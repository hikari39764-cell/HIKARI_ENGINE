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
bool DocumentSceneBase::PreviewSequence(const CinematicSequence& sequence,
                                        const SEQUENCER::SequenceBindingContext& bindings,
                                        const SEQUENCER::SequenceEvaluationContext& context, bool contentChanged) {
    if (state_->runtime.playActive)
        return false;
    auto& camera = state_->camera;
    if (camera.sequencePreview.IsValid() && !camera.sequencePlayback.IsActive(camera.sequencePreview))
        camera.sequencePreview = {};
    if (!camera.sequencePreviewSnapshotActive) {
        EndEditorCameraPreview();
        camera.sequencePreviewSnapshot = camera.editorCamera;
        camera.sequencePreviewSnapshotActive = true;
    }
    if (contentChanged && camera.sequencePreview.IsValid()) {
        camera.sequencePlayback.Stop(camera.sequencePreview);
        camera.sequencePreview = {};
    }
    if (!camera.sequencePreview.IsValid()) {
        SEQUENCER::SequencePlaybackOptions options;
        options.holdAtEnd = true;
        camera.sequencePreview = camera.sequencePlayback.PlayInline(
            sequence, bindings, options, context.currentTimeSeconds, "Editor.SequencePreview", 1000);
    }
    if (camera.sequencePlayback.EvaluateExternal(camera.sequencePreview, context))
        return true;
    EndSequencePreview();
    return false;
}

void DocumentSceneBase::EndSequencePreview() {
    auto& camera = state_->camera;
    if (!camera.sequencePreviewSnapshotActive)
        return;
    if (camera.sequencePreview.IsValid())
        camera.sequencePlayback.Stop(camera.sequencePreview);
    camera.sequencePreview = {};
    camera.sequencePreviewSnapshotActive = false;
    camera.editorCamera = camera.sequencePreviewSnapshot;
    camera.resolvedFrame.camera = camera.editorCamera;
    camera.editorCameraCutPending = true;
}

bool DocumentSceneBase::ApplyCameraRuntimeChanges() {
    const SceneObjectId defaultCamera =
        state_->identity.document.camera.defaultCameraObjectId.value_or(SceneObjectId{});
    state_->camera.director.SetBaseCamera(defaultCamera);

    if (state_->camera.editorCameraPreviewObjectId.value != 0) {
        const auto previewObject = std::find_if(
            state_->identity.document.objects.begin(), state_->identity.document.objects.end(),
            [this](const SceneObjectData& object) { return object.id == state_->camera.editorCameraPreviewObjectId; });
        bool previewCameraValid = previewObject != state_->identity.document.objects.end();
        if (previewCameraValid) {
            const GameObject* runtimeObject =
                state_->runtime.world.FindObject(state_->camera.editorCameraPreviewObjectId);
            const CameraComponent* camera =
                runtimeObject != nullptr ? runtimeObject->GetComponent<CameraComponent>() : nullptr;
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
        state_->identity.document.objects.begin(), state_->identity.document.objects.end(),
        [cameraObjectId](const SceneObjectData& candidate) {
            if (!(candidate.id == cameraObjectId)) {
                return false;
            }
            return std::any_of(candidate.components.begin(), candidate.components.end(),
                               [](const SceneComponentData& component) { return component.type == "CameraComponent"; });
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
bool DocumentSceneBase::BeginEditorCameraPreview(SceneObjectId cameraObjectId, const CameraBlendDesc& blend,
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

    const bool replacingActivePreview = IsEditorCameraPreviewActive();
    if (replacingActivePreview) {
        (void)state_->camera.director.ReleaseOverride(state_->camera.editorCameraPreviewToken);
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
bool DocumentSceneBase::TryResolveCameraObjectView(SceneObjectId cameraObjectId, float aspect,
                                                   Camera3D& outCamera) const {
    return state_->camera.director.TryResolveCameraObject(state_->runtime.world, cameraObjectId, aspect, outCamera);
}
bool DocumentSceneBase::ApplyCameraObjectPose(SceneObjectId cameraObjectId, const MATH::Vec3& position,
                                              const MATH::Quat& rotation, bool markDirty) {
    if (state_->runtime.playActive || cameraObjectId.value == 0) {
        return false;
    }

    auto documentObject =
        std::find_if(state_->identity.document.objects.begin(), state_->identity.document.objects.end(),
                     [cameraObjectId](const SceneObjectData& object) { return object.id == cameraObjectId; });
    if (documentObject == state_->identity.document.objects.end() || documentObject->parent.has_value()) {
        return false;
    }

    GameObject* runtimeObject = state_->runtime.world.FindObject(cameraObjectId);
    CameraComponent* cameraComponent =
        runtimeObject != nullptr ? runtimeObject->GetComponent<CameraComponent>() : nullptr;
    if (cameraComponent == nullptr) {
        return false;
    }

    const MATH::Quat normalizedRotation = MATH::NormalizeQ(rotation);
    documentObject->transform.position = position;
    documentObject->transform.rotationEulerDeg =
        MATH::EulerXYZDegreesFromQuatNearest(normalizedRotation, documentObject->transform.rotationEulerDeg);
    documentObject->transform.scale = {1.0f, 1.0f, 1.0f};

    Transform3D runtimeTransform = runtimeObject->GetTransform();
    runtimeTransform.position = position;
    runtimeTransform.rotation = normalizedRotation;
    runtimeTransform.scale = {1.0f, 1.0f, 1.0f};
    runtimeTransform.useExplicitMatrix = false;
    (void)runtimeObject->SetLocalTransform(runtimeTransform);

    if (markDirty) {
        state_->identity.documentDirty = true;
    }
    return true;
}
bool DocumentSceneBase::SnapCameraObjectToEditorView(SceneObjectId cameraObjectId) {
    const MATH::Quat rotation =
        MATH::Quat::FromEulerXYZ(-state_->camera.debugCamera.GetPitch(), state_->camera.debugCamera.GetYaw(), 0.0f);
    return ApplyCameraObjectPose(cameraObjectId, state_->camera.debugCamera.GetPosition(), rotation, true);
}
bool DocumentSceneBase::HasRuntimeSceneCameraDriver() const {
    if (state_->identity.document.camera.defaultCameraObjectId.has_value()) {
        const GameObject* object =
            state_->runtime.world.FindObject(state_->identity.document.camera.defaultCameraObjectId.value());
        const CameraComponent* camera = object != nullptr ? object->GetComponent<CameraComponent>() : nullptr;
        if (camera != nullptr && camera->IsEnabled()) {
            return true;
        }
    }

    const auto system =
        std::find_if(state_->identity.document.systems.begin(), state_->identity.document.systems.end(),
                     [](const SceneSystemData& entry) { return entry.systemId == "CameraFollowSystem"; });
    if (system != state_->identity.document.systems.end() && !system->enabled) {
        return false;
    }

    for (const auto& object : state_->runtime.world.GetObjects()) {
        if (!object) {
            continue;
        }
        const auto* follow = object->GetComponent<CameraFollowComponent>();
        const auto* camera = object->GetComponent<CameraComponent>();
        if (follow != nullptr && follow->IsEnabled() && camera != nullptr && camera->IsEnabled()) {
            return true;
        }
    }
    return false;
}

} // namespace HIKARI
