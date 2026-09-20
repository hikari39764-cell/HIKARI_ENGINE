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
            const int64_t combined =
                static_cast<int64_t>(kCameraSequenceBasePriority) +
                static_cast<int64_t>(instancePriority);
            return static_cast<int>(std::clamp(
                combined,
                static_cast<int64_t>((std::numeric_limits<int>::min)()),
                static_cast<int64_t>((std::numeric_limits<int>::max)())));
        }
    }

    CameraSequenceTrackDriver::CameraSequenceTrackDriver(
        CameraDirector& cameraDirector,
        const World& world,
        const Camera3D& viewportCamera, SequencePresentationState* presentation) noexcept
        : presentation_(presentation)
        , cameraDirector_(cameraDirector)
        , world_(world)
        , viewportCamera_(viewportCamera) {
    }

    std::string_view CameraSequenceTrackDriver::GetDriverId() const noexcept {
        return "Camera";
    }

    void CameraSequenceTrackDriver::OnSequenceStarted(
        const SequencePlaybackInstanceView& instance) {

        if (!instance.handle.IsValid() || FindState(instance.handle) != nullptr) {
            return;
        }
        states_.push_back({ instance.handle });
    }

    void CameraSequenceTrackDriver::Validate(
        const SequencePlaybackInstanceView& instance,
        std::vector<SequencePlaybackDiagnostic>& diagnostics) const {

        if (instance.asset == nullptr || instance.bindings == nullptr ||
            !instance.asset->sequence.cameraCutTrack.enabled) {
            return;
        }
        std::unordered_set<uint64_t> validatedObjectBindings{};
        std::unordered_set<uint64_t> validatedCameraBindings{};
        for (const CameraCutClip& clip :
                instance.asset->sequence.cameraCutTrack.clips) {
            if (clip.camera.owned) {
                for (const auto id : {clip.camera.anchor, clip.camera.lookAt}) {
                    if (!id.IsValid() || !validatedObjectBindings.insert(id.value).second) continue;
                    SceneObjectId objectId;
                    if (!instance.bindings->Resolve(instance.asset->sequence.bindings,id,objectId) || !world_.FindObject(objectId))
                        diagnostics.push_back({SequenceDiagnosticSeverity::Error,"ShotBindingMissing","Camera",
                            "Shot anchor or look-at object is unavailable",id});
                }
                continue;
            }
            if (!clip.cameraBindingId.IsValid() ||
                !validatedCameraBindings.insert(
                    clip.cameraBindingId.value).second) {
                continue;
            }
            SceneObjectId cameraObjectId{};
            if (!instance.bindings->Resolve(
                    instance.asset->sequence.bindings,
                    clip.cameraBindingId,
                    cameraObjectId)) {
                continue;
            }
            const GameObject* cameraObject =
                world_.FindObject(cameraObjectId);
            const CameraComponent* camera = cameraObject
                ? cameraObject->GetComponent<CameraComponent>()
                : nullptr;
            if (camera != nullptr && camera->IsEnabled()) {
                continue;
            }
            const SequenceBinding* binding = FindSequenceBinding(
                instance.asset->sequence.bindings,
                clip.cameraBindingId);
            diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                cameraObject == nullptr
                    ? "CameraBindingObjectMissing"
                    : "CameraBindingComponentMissing",
                "Camera",
                cameraObject == nullptr
                    ? "Camera binding resolves to an object that is not in the active scene"
                    : "Camera binding target has no enabled CameraComponent",
                clip.cameraBindingId,
                binding != nullptr ? binding->slotName : std::string{}
            });
        }
    }

    void CameraSequenceTrackDriver::Evaluate(
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        if (instance.asset == nullptr || instance.bindings == nullptr) {
            return;
        }
        InstanceState* state = FindState(instance.handle);
        if (state == nullptr) {
            states_.push_back({ instance.handle });
            state = &states_.back();
        }

        const CinematicCameraEvaluation evaluation =
            EvaluateCinematicCameraTrack(
                instance.asset->sequence,
                context.currentTimeSeconds,
                *instance.bindings);
        const auto& sequence = instance.asset->sequence;
        Camera3D evaluatedCamera;
        if (!ResolveView(sequence, *instance.bindings, evaluation, evaluatedCamera)) {
            if (context.reason == SequenceEvaluationReason::Complete && state->overrideToken.IsValid()) return;
            ReleaseOverride(*state);
            return;
        }
        const auto shot = std::find_if(sequence.cameraCutTrack.clips.begin(), sequence.cameraCutTrack.clips.end(),
            [&](const auto& clip) { return clip.id == evaluation.shotId; });
        if (shot != sequence.cameraCutTrack.clips.end() &&
            evaluation.transition.mode == CameraCutTransitionMode::EaseInOut &&
            evaluation.transition.durationSeconds > 0 && evaluation.localTimeSeconds < evaluation.transition.durationSeconds) {
            const float previousTime = std::nextafter(shot->startTimeSeconds, -1.0f);
            const auto previous = EvaluateCinematicCameraTrack(sequence, previousTime, *instance.bindings);
            Camera3D from;
            if (previous.shotId != evaluation.shotId && ResolveView(sequence, *instance.bindings, previous, from)) {
                const float t = std::clamp(evaluation.localTimeSeconds / evaluation.transition.durationSeconds, 0.0f, 1.0f);
                evaluatedCamera = CameraDirector::BlendViews(from, evaluatedCamera, t*t*(3-2*t));
            }
        }
        const auto submitPresentation = [&]() {
            if (!presentation_ || !state->overrideToken.IsValid()) return;
            std::vector<ScreenEffectSample> samples;
            if (shot != sequence.cameraCutTrack.clips.end())
                SampleScreenEffects(shot->effects, evaluation.localTimeSeconds, samples);
            SampleScreenEffects(sequence.screenEffects, context.currentTimeSeconds, samples);
            presentation_->Set(state->overrideToken, std::move(samples));
        };
        if (state->overrideToken.IsValid() && !context.IsDiscontinuous() &&
            state->cameraObjectId == evaluation.cameraObjectId &&
            state->shotId == evaluation.shotId) {
            (void)cameraDirector_.SetOverrideCamera(state->overrideToken, evaluatedCamera);
            submitPresentation();
            return;
        }

        ReleaseOverride(*state);
        CameraActivationRequest request{};
        request.cameraObjectId = evaluation.cameraObjectId;
        request.blend = {}; // authored transitions are evaluated from sequence time, including seeks
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

    bool CameraSequenceTrackDriver::ResolveView(const CinematicSequence& sequence,
        const SequenceBindingContext& bindings, const CinematicCameraEvaluation& evaluation, Camera3D& out) const {
        if (!evaluation.IsValid()) return false;
        const float aspect = viewportCamera_.GetAspect();
        if (!evaluation.ownedCamera) {
            Camera3D source;
            if (!cameraDirector_.TryResolveCameraObject(world_,evaluation.cameraObjectId,aspect,source)) return false;
            out=source;
            if (evaluation.HasCameraAnimation()) BuildEvaluatedCinematicCamera(evaluation,source,aspect,out);
            return true;
        }
        for (const auto& clip : sequence.cameraCutTrack.clips) if (clip.id==evaluation.shotId) {
            MATH::Mat4 anchor;
            MATH::Vec3 target;
            const auto resolveObject = [&](SequenceBindingId binding) -> const GameObject* {
                SceneObjectId id;
                return binding.IsValid() && bindings.Resolve(sequence.bindings, binding, id)
                    ? world_.FindObject(id) : nullptr;
            };
            const auto* anchorObject = resolveObject(clip.camera.anchor);
            const auto* targetObject = resolveObject(clip.camera.lookAt);
            if (anchorObject) anchor = anchorObject->GetTransform().GetWorldMatrix();
            if (targetObject) {
                const auto& m = targetObject->GetTransform().GetWorldMatrix();
                target = {m.m[3][0],m.m[3][1],m.m[3][2]};
            }
            out = EvaluateShotCamera(clip.camera, evaluation.localTimeSeconds, aspect,
                anchorObject ? &anchor : nullptr, targetObject ? &target : nullptr);
            return true;
        }
        return false;
    }

    void CameraSequenceTrackDriver::OnSequenceStopped(
        SequencePlaybackHandle handle,
        SequenceStopReason) {

        const auto found = std::find_if(
            states_.begin(),
            states_.end(),
            [handle](const InstanceState& state) {
                return state.handle == handle;
            });
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

    CameraSequenceTrackDriver::InstanceState*
        CameraSequenceTrackDriver::FindState(
            SequencePlaybackHandle handle) noexcept {

        const auto found = std::find_if(
            states_.begin(),
            states_.end(),
            [handle](const InstanceState& state) {
                return state.handle == handle;
            });
        return found != states_.end() ? &*found : nullptr;
    }

    void CameraSequenceTrackDriver::ReleaseOverride(
        InstanceState& state) noexcept {

        if (presentation_) presentation_->Erase(state.overrideToken);
        if (state.overrideToken.IsValid()) {
            (void)cameraDirector_.ReleaseOverride(state.overrideToken);
        }
        state.overrideToken = {};
        state.cameraObjectId = {};
        state.shotId = 0;
    }

} // namespace HIKARI::SEQUENCER
