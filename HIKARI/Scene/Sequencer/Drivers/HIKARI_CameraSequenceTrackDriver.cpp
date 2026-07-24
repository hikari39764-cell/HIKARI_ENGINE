#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"

#include <algorithm>
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

        CameraBlendDesc ToCameraBlendDesc(
            const CameraCutTransition& transition,
            const SequenceEvaluationContext& context) {

            CameraBlendDesc blend{};
            if (context.IsDiscontinuous() ||
                transition.mode == CameraCutTransitionMode::Cut) {
                return blend;
            }
            blend.mode = CameraBlendMode::EaseInOut;
            blend.durationSeconds = transition.durationSeconds;
            return blend;
        }

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
        const Camera3D& viewportCamera) noexcept
        : cameraDirector_(cameraDirector)
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
        std::unordered_set<uint64_t> validatedBindings{};
        for (const CameraCutClip& clip :
                instance.asset->sequence.cameraCutTrack.clips) {
            if (!clip.cameraBindingId.IsValid() ||
                !validatedBindings.insert(
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
        Camera3D resolvedCamera{};
        const float aspect = viewportCamera_.GetAspect();
        const bool cameraValid = evaluation.IsValid() &&
            cameraDirector_.TryResolveCameraObject(
                world_,
                evaluation.cameraObjectId,
                aspect,
                resolvedCamera);
        if (!cameraValid) {
            ReleaseOverride(*state);
            return;
        }

        Camera3D evaluatedCamera{};
        const bool hasCameraAnimation =
            evaluation.HasCameraAnimation() &&
            BuildEvaluatedCinematicCamera(
                evaluation,
                resolvedCamera,
                aspect,
                evaluatedCamera);
        if (state->overrideToken.IsValid() &&
            state->cameraObjectId == evaluation.cameraObjectId &&
            state->shotId == evaluation.shotId) {
            if (hasCameraAnimation) {
                (void)cameraDirector_.SetOverrideCamera(
                    state->overrideToken,
                    evaluatedCamera);
            } else {
                (void)cameraDirector_.ClearOverrideCamera(
                    state->overrideToken);
            }
            return;
        }

        ReleaseOverride(*state);
        CameraActivationRequest request{};
        request.cameraObjectId = evaluation.cameraObjectId;
        request.blend = ToCameraBlendDesc(evaluation.transition, context);
        request.priority = ToCameraPriority(instance.priority);
        request.affectsControlBasis = false;
        state->overrideToken = cameraDirector_.PushOverride(request);
        if (!state->overrideToken.IsValid()) {
            return;
        }
        state->cameraObjectId = evaluation.cameraObjectId;
        state->shotId = evaluation.shotId;
        if (hasCameraAnimation) {
            (void)cameraDirector_.SetOverrideCamera(
                state->overrideToken,
                evaluatedCamera);
        }
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

        if (state.overrideToken.IsValid()) {
            (void)cameraDirector_.ReleaseOverride(state.overrideToken);
        }
        state.overrideToken = {};
        state.cameraObjectId = {};
        state.shotId = 0;
    }

} // namespace HIKARI::SEQUENCER
