#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <vector>
#include <utility>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"

namespace HIKARI {
    bool DocumentSceneBase::ApplyCameraRuntimeChanges() {
        const SceneObjectId defaultCamera = state_->identity.document.camera.defaultCameraObjectId.value_or(SceneObjectId{});
        state_->camera.director.SetBaseCamera(defaultCamera);

        if (state_->camera.editorCameraPreviewObjectId.value != 0) {
            const auto previewObject = std::find_if(
                state_->identity.document.objects.begin(),
                state_->identity.document.objects.end(),
                [this](const SceneObjectData& object) {
                    return object.id == state_->camera.editorCameraPreviewObjectId;
                });
            bool previewCameraValid = previewObject != state_->identity.document.objects.end();
            if (previewCameraValid) {
                const GameObject* runtimeObject =
                    state_->runtime.world.FindObject(state_->camera.editorCameraPreviewObjectId);
                const CameraComponent* camera = runtimeObject != nullptr
                    ? runtimeObject->GetComponent<CameraComponent>()
                    : nullptr;
                previewCameraValid = camera != nullptr && camera->IsEnabled();
            }
            if (!previewCameraValid) {
                EndEditorCameraPreview();
            }
        }

        if (state_->runtime.playActive) {
            state_->camera.runtimeSceneCameraActive = HasRuntimeSceneCameraDriver();
            state_->camera.runtimePreviewCameraActive = !state_->camera.runtimeSceneCameraActive;
        }
        return true;
    }
    bool DocumentSceneBase::SetGameDefaultCamera(SceneObjectId cameraObjectId) {
        if (cameraObjectId.value == 0) {
            return false;
        }

        const auto object = std::find_if(
            state_->identity.document.objects.begin(),
            state_->identity.document.objects.end(),
            [cameraObjectId](const SceneObjectData& candidate) {
                if (!(candidate.id == cameraObjectId)) {
                    return false;
                }
                return std::any_of(
                    candidate.components.begin(),
                    candidate.components.end(),
                    [](const SceneComponentData& component) {
                        return component.type == "CameraComponent";
                    });
            });
        if (object == state_->identity.document.objects.end()) {
            return false;
        }

        if (!state_->identity.document.camera.defaultCameraObjectId.has_value() ||
            !(state_->identity.document.camera.defaultCameraObjectId.value() == cameraObjectId)) {
            state_->identity.document.camera.defaultCameraObjectId = cameraObjectId;
            state_->identity.documentDirty = true;
        }
        return ApplyCameraRuntimeChanges();
    }
    void DocumentSceneBase::ClearGameDefaultCamera() {
        if (!state_->identity.document.camera.defaultCameraObjectId.has_value()) {
            return;
        }
        state_->identity.document.camera.defaultCameraObjectId.reset();
        state_->identity.documentDirty = true;
        ApplyCameraRuntimeChanges();
    }
    bool DocumentSceneBase::BeginEditorCameraPreview(
        SceneObjectId cameraObjectId,
        const CameraBlendDesc& blend,
        bool forceRestart) {

        if (state_->runtime.playActive || cameraObjectId.value == 0) {
            return false;
        }
        if (!forceRestart && IsEditorCameraPreviewActive() &&
            state_->camera.editorCameraPreviewObjectId == cameraObjectId) {
            return true;
        }

        GameObject* cameraObject = state_->runtime.world.FindObject(cameraObjectId);
        const CameraComponent* cameraComponent =
            cameraObject != nullptr ? cameraObject->GetComponent<CameraComponent>() : nullptr;
        if (cameraComponent == nullptr || !cameraComponent->IsEnabled()) {
            return false;
        }

        const bool replacingActivePreview =
            IsEditorCameraPreviewActive();
        if (replacingActivePreview) {
            (void)state_->camera.director.ReleaseOverride(
                state_->camera.editorCameraPreviewToken);
            state_->camera.editorCameraPreviewObjectId = {};
            state_->camera.editorCameraPreviewToken = {};
        } else {
            state_->camera.editorCameraPreviewSnapshot = state_->camera.editorCamera;
        }
        state_->camera.director.SetBaseCamera(
            state_->identity.document.camera.defaultCameraObjectId.value_or(SceneObjectId{}));
        CameraActivationRequest request{};
        request.cameraObjectId = cameraObjectId;
        request.blend = blend;
        request.priority = 1000;
        request.affectsControlBasis = false;
        state_->camera.editorCameraPreviewToken = state_->camera.director.PushOverride(request);
        if (!state_->camera.editorCameraPreviewToken.IsValid()) {
            return false;
        }
        state_->camera.editorCameraPreviewObjectId = cameraObjectId;
        return true;
    }
    bool DocumentSceneBase::UpdateEditorCameraPreview(
        const CinematicCameraEvaluation& evaluation) {

        if (!IsEditorCameraPreviewActive() || !evaluation.IsValid() ||
            !(state_->camera.editorCameraPreviewObjectId == evaluation.cameraObjectId)) {
            return false;
        }
        if (!evaluation.HasCameraAnimation()) {
            return state_->camera.director.ClearOverrideCamera(
                state_->camera.editorCameraPreviewToken);
        }

        Camera3D sourceCamera{};
        if (!TryResolveCameraObjectView(
                evaluation.cameraObjectId,
                state_->camera.editorCamera.GetAspect(),
                sourceCamera)) {
            return false;
        }
        Camera3D evaluatedCamera{};
        return BuildEvaluatedCinematicCamera(
                evaluation,
                sourceCamera,
                state_->camera.editorCamera.GetAspect(),
                evaluatedCamera) &&
            state_->camera.director.SetOverrideCamera(
                state_->camera.editorCameraPreviewToken,
                evaluatedCamera);
    }
    void DocumentSceneBase::EndEditorCameraPreview() {
        if (!IsEditorCameraPreviewActive()) {
            state_->camera.editorCameraPreviewObjectId = {};
            state_->camera.editorCameraPreviewToken = {};
            return;
        }
        (void)state_->camera.director.ReleaseOverride(state_->camera.editorCameraPreviewToken);
        state_->camera.editorCameraPreviewObjectId = {};
        state_->camera.editorCameraPreviewToken = {};
        state_->camera.editorCamera = state_->camera.editorCameraPreviewSnapshot;
        state_->camera.resolvedFrame.camera = state_->camera.editorCamera;
        state_->camera.resolvedFrame.sourceCameraObjectId = 0;
        state_->camera.resolvedFrame.valid = true;
        state_->camera.editorCameraCutPending = true;
    }
    bool DocumentSceneBase::IsEditorCameraPreviewActive() const {
        return state_->camera.editorCameraPreviewObjectId.value != 0 && state_->camera.editorCameraPreviewToken.IsValid();
    }
    SceneObjectId DocumentSceneBase::GetEditorCameraPreviewObjectId() const {
        return state_->camera.editorCameraPreviewObjectId;
    }
    bool DocumentSceneBase::TryResolveCameraObjectView(
        SceneObjectId cameraObjectId,
        float aspect,
        Camera3D& outCamera) const {

        return state_->camera.director.TryResolveCameraObject(
            state_->runtime.world,
            cameraObjectId,
            aspect,
            outCamera);
    }
    bool DocumentSceneBase::ApplyCameraObjectPose(
        SceneObjectId cameraObjectId,
        const MATH::Vec3& position,
        const MATH::Quat& rotation,
        bool markDirty) {

        if (state_->runtime.playActive || cameraObjectId.value == 0) {
            return false;
        }

        auto documentObject = std::find_if(
            state_->identity.document.objects.begin(),
            state_->identity.document.objects.end(),
            [cameraObjectId](const SceneObjectData& object) {
                return object.id == cameraObjectId;
            });
        if (documentObject == state_->identity.document.objects.end() ||
            documentObject->parent.has_value()) {
            return false;
        }

        GameObject* runtimeObject = state_->runtime.world.FindObject(cameraObjectId);
        CameraComponent* cameraComponent =
            runtimeObject != nullptr
                ? runtimeObject->GetComponent<CameraComponent>()
                : nullptr;
        if (cameraComponent == nullptr) {
            return false;
        }

        const MATH::Quat normalizedRotation = MATH::NormalizeQ(rotation);
        documentObject->transform.position = position;
        documentObject->transform.rotationEulerDeg =
            MATH::EulerXYZDegreesFromQuatNearest(
                normalizedRotation,
                documentObject->transform.rotationEulerDeg);
        documentObject->transform.scale = { 1.0f, 1.0f, 1.0f };

        Transform3D runtimeTransform = runtimeObject->GetTransform();
        runtimeTransform.position = position;
        runtimeTransform.rotation = normalizedRotation;
        runtimeTransform.scale = { 1.0f, 1.0f, 1.0f };
        runtimeTransform.useExplicitMatrix = false;
        (void)runtimeObject->SetLocalTransform(runtimeTransform);

        if (markDirty) {
            state_->identity.documentDirty = true;
        }
        return true;
    }
    bool DocumentSceneBase::SnapCameraObjectToEditorView(SceneObjectId cameraObjectId) {
        const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
            -state_->camera.debugCamera.GetPitch(),
            state_->camera.debugCamera.GetYaw(),
            0.0f);
        return ApplyCameraObjectPose(
            cameraObjectId,
            state_->camera.debugCamera.GetPosition(),
            rotation,
            true);
    }
    CinematicPlaybackHandle DocumentSceneBase::PlayCameraSequence(
        CinematicSequenceId sequenceId,
        const CinematicPlaybackOptions& options,
        float startTimeSeconds) {

        if (!state_->runtime.playActive ||
            !IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)) {
            return {};
        }
        const CinematicSequence* sequence = FindCinematicSequence(
            state_->identity.document.cinematics,
            sequenceId);
        if (sequence == nullptr) {
            return {};
        }
        state_->camera.currentCameraSequenceHandle =
            state_->camera.sequencePlayback.PlayInline(
                *sequence,
                {},
                options,
                startTimeSeconds,
                "Camera.Sequence",
                0,
                SEQUENCER::SequenceChannelPolicy::Replace);
        return state_->camera.currentCameraSequenceHandle;
    }
    bool DocumentSceneBase::StopCameraSequence(
        CinematicPlaybackHandle handle) {

        const bool stopped = state_->camera.sequencePlayback.Stop(handle);
        if (stopped && state_->camera.currentCameraSequenceHandle == handle) {
            state_->camera.currentCameraSequenceHandle = {};
        }
        return stopped;
    }
    bool DocumentSceneBase::PauseCameraSequence(
        CinematicPlaybackHandle handle) {

        return state_->camera.sequencePlayback.Pause(handle);
    }
    bool DocumentSceneBase::ResumeCameraSequence(
        CinematicPlaybackHandle handle) {

        return state_->camera.sequencePlayback.Resume(handle);
    }
    bool DocumentSceneBase::SeekCameraSequence(
        CinematicPlaybackHandle handle,
        float timeSeconds) {

        return state_->camera.sequencePlayback.Seek(handle, timeSeconds);
    }
    bool DocumentSceneBase::IsCameraSequencePlaying() const noexcept {
        return state_->camera.sequencePlayback.IsPlaying(
            state_->camera.currentCameraSequenceHandle);
    }
    CinematicPlaybackHandle
    DocumentSceneBase::GetCameraSequencePlaybackHandle() const noexcept {
        return state_->camera.currentCameraSequenceHandle;
    }
    SEQUENCER::SequencePlaybackHandle DocumentSceneBase::PlaySequence(
        const SEQUENCER::SequencePlayRequest& request) {

        return state_->runtime.playActive &&
            IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)
            ? state_->camera.sequencePlayback.Play(request)
            : SEQUENCER::SequencePlaybackHandle{};
    }
    SEQUENCER::SequencePlayResult
        DocumentSceneBase::PlaySequenceDetailed(
            const SEQUENCER::SequencePlayRequest& request) {

        if (!state_->runtime.playActive) {
            SEQUENCER::SequencePlayResult result{};
            result.diagnostics.push_back({
                SEQUENCER::SequenceDiagnosticSeverity::Error,
                "RuntimePlayInactive",
                {},
                "Sequence playback requires an active runtime Play session"
            });
            return result;
        }
        if (!IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)) {
            SEQUENCER::SequencePlayResult result{};
            result.diagnostics.push_back({
                SEQUENCER::SequenceDiagnosticSeverity::Error,
                "RuntimeFeatureDisabled",
                {},
                "Sequence playback requires the Cinematics runtime feature"
            });
            return result;
        }
        return state_->camera.sequencePlayback.PlayDetailed(request);
    }
    bool DocumentSceneBase::StopSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return state_->camera.sequencePlayback.Stop(handle);
    }
    bool DocumentSceneBase::PauseSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return state_->camera.sequencePlayback.Pause(handle);
    }
    bool DocumentSceneBase::ResumeSequence(
        SEQUENCER::SequencePlaybackHandle handle) {

        return state_->camera.sequencePlayback.Resume(handle);
    }
    bool DocumentSceneBase::SeekSequence(
        SEQUENCER::SequencePlaybackHandle handle,
        float timeSeconds) {

        return state_->camera.sequencePlayback.Seek(handle, timeSeconds);
    }
    uint64_t DocumentSceneBase::SubmitSequenceCommand(
        SEQUENCER::SequencePlaybackCommand command) {

        return state_->runtime.playActive &&
            IsRuntimeFeatureActive(RuntimeFeatureIds::Cinematics)
            ? state_->camera.sequencePlayback.Submit(std::move(command))
            : 0;
    }
    bool DocumentSceneBase::IsSequencePlaying(
        SEQUENCER::SequencePlaybackHandle handle) const noexcept {

        return state_->camera.sequencePlayback.IsPlaying(handle);
    }
    bool DocumentSceneBase::IsSequenceActive(
        SEQUENCER::SequencePlaybackHandle handle) const noexcept {

        return state_->camera.sequencePlayback.IsActive(handle);
    }
    bool DocumentSceneBase::TryGetSequencePlaybackSnapshot(
        SEQUENCER::SequencePlaybackHandle handle,
        SEQUENCER::SequencePlaybackSnapshot& outSnapshot) const noexcept {

        return state_->camera.sequencePlayback.TryGetSnapshot(
            handle,
            outSnapshot);
    }
    std::vector<SEQUENCER::SequencePlaybackEvent>
        DocumentSceneBase::ConsumeSequencePlaybackEvents() {

        return state_->camera.sequencePlayback.ConsumeEvents();
    }
    bool DocumentSceneBase::HasRuntimeSceneCameraDriver() const {
        if (state_->identity.document.camera.defaultCameraObjectId.has_value()) {
            const GameObject* object = state_->runtime.world.FindObject(
                state_->identity.document.camera.defaultCameraObjectId.value());
            const CameraComponent* camera = object != nullptr
                ? object->GetComponent<CameraComponent>()
                : nullptr;
            if (camera != nullptr && camera->IsEnabled()) {
                return true;
            }
        }

        const auto system = std::find_if(
            state_->identity.document.systems.begin(),
            state_->identity.document.systems.end(),
            [](const SceneSystemData& entry) {
                return entry.systemId == "CameraFollowSystem";
            });
        if (system != state_->identity.document.systems.end() && !system->enabled) {
            return false;
        }

        for (const auto& object : state_->runtime.world.GetObjects()) {
            if (!object) {
                continue;
            }
            const auto* follow = object->GetComponent<CameraFollowComponent>();
            const auto* camera = object->GetComponent<CameraComponent>();
            if (follow != nullptr && follow->IsEnabled() &&
                camera != nullptr && camera->IsEnabled()) {
                return true;
            }
        }
        return false;
    }

} // namespace HIKARI
