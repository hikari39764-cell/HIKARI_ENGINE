#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::SEQUENCER {

namespace {
constexpr int kCameraSequenceBasePriority = 100;

int ToCameraPriority(int instancePriority) noexcept {
    const int64_t combined = static_cast<int64_t>(kCameraSequenceBasePriority) + static_cast<int64_t>(instancePriority);
    return static_cast<int>(std::clamp(combined, static_cast<int64_t>((std::numeric_limits<int>::min)()),
                                       static_cast<int64_t>((std::numeric_limits<int>::max)())));
}
} // namespace

CameraSequenceTrackDriver::CameraSequenceTrackDriver(CameraDirector& cameraDirector, const World& world,
                                                     const Camera3D& viewportCamera,
                                                     SequencePresentationState* presentation) noexcept
    : presentation_(presentation), cameraDirector_(cameraDirector), world_(world), viewportCamera_(viewportCamera) {}

std::string_view CameraSequenceTrackDriver::GetDriverId() const noexcept { return "Camera"; }

void CameraSequenceTrackDriver::OnSequenceStarted(const SequencePlaybackInstanceView& instance) {
    if (!instance.handle.IsValid() || FindState(instance.handle) != nullptr) {
        return;
    }
    states_.push_back({instance.handle});
    auto& state = states_.back();
    state.entryCamera =
        cameraDirector_.GetResolvedFrame().valid ? cameraDirector_.GetResolvedFrame().camera : viewportCamera_;
    if (const auto* camera = world_.FindObject(cameraDirector_.GetBaseCamera())) {
        if (const auto* follow = camera->GetComponent<CameraFollowComponent>()) {
            auto profile = follow->GameplayState().source;
            if (!profile)
                if (const auto* store = world_.Services().Find<GameplayCameraAssetStore>()) {
                    std::string error;
                    profile = store->Load(follow->GameplayBinding().profile, error);
                }
            if (profile) {
                state.entrySeconds = profile->cinematicEnterSeconds;
                state.returnSeconds = profile->cinematicReturnSeconds;
            }
        }
    }
}

void CameraSequenceTrackDriver::Validate(const SequencePlaybackInstanceView& instance,
                                         std::vector<SequencePlaybackDiagnostic>& diagnostics) const {
    if (instance.asset == nullptr || instance.bindings == nullptr || !instance.asset->sequence.cameraCutTrack.enabled) {
        return;
    }
    std::unordered_set<uint64_t> validatedObjectBindings{};
    std::unordered_set<uint64_t> validatedCameraBindings{};
    for (const CameraCutClip& clip : instance.asset->sequence.cameraCutTrack.clips) {
        std::vector<SequenceBindingId> references{clip.camera.anchor, clip.camera.lookAt, clip.camera.base.reference};
        for (const auto& key : clip.camera.keys)
            references.push_back(key.reference);
        for (const auto id : references) {
            if (!id.IsValid() || !validatedObjectBindings.insert(id.value).second)
                continue;
            SceneObjectId objectId;
            if (!instance.bindings->Resolve(instance.asset->sequence.bindings, id, objectId) ||
                !world_.FindObject(objectId))
                diagnostics.push_back({SequenceDiagnosticSeverity::Error, "ShotBindingMissing", "Camera",
                                       "Shot reference, anchor or look-at object is unavailable", id});
        }
        const auto validatePose = [&](const ShotPose& pose) {
            if (!pose.reference.IsValid())
                return;
            SceneObjectId id;
            Camera3D view;
            if (!instance.bindings->Resolve(instance.asset->sequence.bindings, pose.reference, id) ||
                !cameraDirector_.TryResolveCameraObject(world_, id, viewportCamera_.GetAspect(), view))
                diagnostics.push_back({SequenceDiagnosticSeverity::Error, "ShotCameraReferenceMissing", "Camera",
                                       "Shared pose requires an enabled CameraComponent", pose.reference});
        };
        validatePose(clip.camera.base);
        for (const auto& key : clip.camera.keys)
            validatePose(key);
        if (!clip.cameraBindingId.IsValid() || !validatedCameraBindings.insert(clip.cameraBindingId.value).second) {
            continue;
        }
        SceneObjectId cameraObjectId{};
        if (!instance.bindings->Resolve(instance.asset->sequence.bindings, clip.cameraBindingId, cameraObjectId)) {
            continue;
        }
        const GameObject* cameraObject = world_.FindObject(cameraObjectId);
        const CameraComponent* camera = cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
        if (camera != nullptr && camera->IsEnabled()) {
            continue;
        }
        const SequenceBinding* binding = FindSequenceBinding(instance.asset->sequence.bindings, clip.cameraBindingId);
        diagnostics.push_back(
            {SequenceDiagnosticSeverity::Error,
             cameraObject == nullptr ? "CameraBindingObjectMissing" : "CameraBindingComponentMissing", "Camera",
             cameraObject == nullptr ? "Camera binding resolves to an object that is not in the active scene"
                                     : "Camera binding target has no enabled CameraComponent",
             clip.cameraBindingId, binding != nullptr ? binding->slotName : std::string{}});
    }
}

void CameraSequenceTrackDriver::Evaluate(const SequencePlaybackInstanceView& instance,
                                         const SequenceEvaluationContext& context) {
    if (instance.asset == nullptr || instance.bindings == nullptr) {
        return;
    }
    InstanceState* state = FindState(instance.handle);
    if (state == nullptr) {
        states_.push_back({instance.handle});
        state = &states_.back();
    }

    const auto& sequence = instance.asset->sequence;
    CinematicCameraEvaluation evaluation;
    Camera3D evaluatedCamera;
    if (!EvaluateSequenceCameraView(sequence, *instance.bindings, world_, cameraDirector_, context.currentTimeSeconds,
                                    viewportCamera_.GetAspect(), evaluation, evaluatedCamera)) {
        if (context.reason == SequenceEvaluationReason::Complete && state->overrideToken.IsValid())
            return;
        ReleaseOverride(*state);
        return;
    }
    if (context.IsDiscontinuous())
        state->entryFinished = true;
    if (state->firstTime < 0)
        state->firstTime = context.currentTimeSeconds;
    if (!state->entryFinished && state->entrySeconds > 0) {
        const float elapsed = (std::max)(0.0f, context.currentTimeSeconds - state->firstTime);
        const float weight = std::clamp(elapsed / state->entrySeconds, 0.0f, 1.0f);
        evaluatedCamera =
            CameraDirector::BlendViews(state->entryCamera, evaluatedCamera, weight * weight * (3 - 2 * weight));
        state->entryFinished = weight >= 1;
    }
    const auto shot = std::find_if(sequence.cameraCutTrack.clips.begin(), sequence.cameraCutTrack.clips.end(),
                                   [&](const auto& clip) { return clip.id == evaluation.shotId; });
    const auto submitPresentation = [&]() {
        if (!presentation_ || !state->overrideToken.IsValid())
            return;
        std::vector<ScreenEffectSample> samples;
        if (shot != sequence.cameraCutTrack.clips.end())
            SampleScreenEffects(shot->effects, evaluation.localTimeSeconds + shot->sourceOffsetSeconds, samples);
        SampleScreenEffects(sequence.screenEffects, context.currentTimeSeconds, samples);
        presentation_->Set(state->overrideToken, std::move(samples));
    };
    if (state->overrideToken.IsValid() && !context.IsDiscontinuous() &&
        state->cameraObjectId == evaluation.cameraObjectId && state->shotId == evaluation.shotId) {
        (void)cameraDirector_.SetOverrideCamera(state->overrideToken, evaluatedCamera);
        submitPresentation();
        return;
    }

    ReleaseOverride(*state);
    CameraActivationRequest request{};
    request.cameraObjectId = evaluation.cameraObjectId;
    request.blend = {}; // authored transitions are evaluated from sequence time, including seeks
    // 初回 tick の移動はカットではない。同じ shot 内の前時刻だけを照合し、
    // 別 shot・seek・loop を連続交代として扱わない。評価時刻と運動曲線は変えない。
    if (!context.IsDiscontinuous() && context.previousTimeSeconds != context.currentTimeSeconds) {
        CinematicCameraEvaluation originEvaluation;
        Camera3D origin;
        if (EvaluateSequenceCameraView(sequence, *instance.bindings, world_, cameraDirector_,
                                       context.previousTimeSeconds, viewportCamera_.GetAspect(), originEvaluation,
                                       origin) &&
            originEvaluation.shotId == evaluation.shotId)
            request.continuityOrigin = origin;
    }
    if (state->returnSeconds > 0)
        request.returnBlend = CameraBlendDesc{CameraBlendMode::EaseInOut, state->returnSeconds};
    request.priority = ToCameraPriority(instance.priority);
    request.affectsControlBasis = false;
    state->overrideToken = cameraDirector_.PushOverride(request);
    if (!state->overrideToken.IsValid()) {
        return;
    }
    state->cameraObjectId = evaluation.cameraObjectId;
    state->shotId = evaluation.shotId;
    submitPresentation();
    (void)cameraDirector_.SetOverrideCamera(state->overrideToken, evaluatedCamera);
}

void CameraSequenceTrackDriver::OnSequenceStopped(SequencePlaybackHandle handle, SequenceStopReason) {
    const auto found = std::find_if(states_.begin(), states_.end(),
                                    [handle](const InstanceState& state) { return state.handle == handle; });
    if (found == states_.end()) {
        return;
    }
    ReleaseOverride(*found);
    states_.erase(found);
}

void CameraSequenceTrackDriver::Reset() {
    for (InstanceState& state : states_) {
        ReleaseOverride(state);
    }
    states_.clear();
}

CameraSequenceTrackDriver::InstanceState* CameraSequenceTrackDriver::FindState(SequencePlaybackHandle handle) noexcept {
    const auto found = std::find_if(states_.begin(), states_.end(),
                                    [handle](const InstanceState& state) { return state.handle == handle; });
    return found != states_.end() ? &*found : nullptr;
}

void CameraSequenceTrackDriver::ReleaseOverride(InstanceState& state) noexcept {
    if (presentation_)
        presentation_->Erase(state.overrideToken);
    if (state.overrideToken.IsValid()) {
        (void)cameraDirector_.ReleaseOverride(state.overrideToken);
    }
    state.overrideToken = {};
    state.cameraObjectId = {};
    state.shotId = 0;
}

} // namespace HIKARI::SEQUENCER
