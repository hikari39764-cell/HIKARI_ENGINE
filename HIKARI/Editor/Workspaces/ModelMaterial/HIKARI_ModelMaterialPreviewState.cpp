#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::SaveActiveCameraState() {
  if (modelGuid_.IsValid()) {
    (void)cameraStateStore_.Save(EditorWorkspaceId::ModelMaterial, modelGuid_,
                                 cameraController_);
  }
}

void ModelMaterialWorkspaceController::ClosePreviewRequest(
    EditorWorkspaceHost &workspaceHost) const {
#if defined(HIKARI_WITH_EDITOR)
  RENDER3D::EDITORVIEW::ClearRequest(
      workspaceHost.GetModelMaterialPreviewView().renderViewId);
#else
  (void)workspaceHost;
#endif
}

void ModelMaterialWorkspaceController::FitPreviewCamera() {
  const Bounds &bounds = previewScene_.GetBounds();
  if (!BOUNDS::IsUsable(bounds)) {
    return;
  }
  const MATH::Vec3 center = (bounds.min + bounds.max) * 0.5f;
  const float radius =
      (std::max)(MATH::Length(bounds.max - bounds.min) * 0.5f, 0.5f);
  EditorDirectorCameraSettings &navigation = cameraController_.Settings();
  navigation.moveSpeed = std::clamp(radius * 0.75f, 5.0f, 1000.0f);
  navigation.fastMultiplier = 5.0f;
  navigation.wheelMoveStep = std::clamp(radius * 0.12f, 0.5f, 200.0f);
  cameraController_.ApplyViewPreset(
      EditorDirectorCameraViewPreset::Perspective);
  cameraController_.Focus(center, radius * 2.4f);
  Camera3D &camera = cameraController_.GetCamera();
  camera.SetPerspective(camera.GetFovYRad(), camera.GetAspect(),
                        (std::max)(0.001f, radius * 0.001f),
                        (std::max)(100.0f, radius * 20.0f));
  ++cameraRevision_;
  cameraCutPending_ = true;
  SaveActiveCameraState();
}

void ModelMaterialWorkspaceController::ResetPreviewCamera() {
  if (modelGuid_.IsValid()) {
    cameraStateStore_.Erase(EditorWorkspaceId::ModelMaterial, modelGuid_);
  }
  FitPreviewCamera();
}

void ModelMaterialWorkspaceController::FocusSelectedSlot() {
  const ModelAsset *model = previewScene_.GetModel();
  if (model == nullptr || !selectedMaterialIndex_) {
    return;
  }
  const std::vector<MATH::Mat4> globals =
      BOUNDS::BuildModelNodeGlobals(*model);
  Bounds selectedBounds = BOUNDS::EmptyBounds();
  for (size_t nodeIndex = 0u; nodeIndex < model->nodes.size(); ++nodeIndex) {
    const ModelNode &node = model->nodes[nodeIndex];
    if (node.meshIndex < 0 ||
        static_cast<size_t>(node.meshIndex) >= model->meshes.size()) {
      continue;
    }
    for (const MeshPrimitive &primitive :
         model->meshes[static_cast<size_t>(node.meshIndex)].primitives) {
      if (primitive.materialIndex == *selectedMaterialIndex_) {
        BOUNDS::Encapsulate(
            selectedBounds,
            BOUNDS::TransformBounds(primitive.bounds, globals[nodeIndex]));
      }
    }
  }
  if (!BOUNDS::IsUsable(selectedBounds)) {
    FitPreviewCamera();
    return;
  }
  const MATH::Vec3 center = (selectedBounds.min + selectedBounds.max) * 0.5f;
  const float radius =
      (std::max)(MATH::Length(selectedBounds.max - selectedBounds.min) * 0.5f,
                 0.25f);
  cameraController_.Focus(center, radius * 2.4f);
  ++cameraRevision_;
  cameraCutPending_ = true;
  SaveActiveCameraState();
}

void ModelMaterialWorkspaceController::InitializePreviewLighting() {
  if (previewLightingInitialized_) {
    return;
  }
  previewEnvironment_ = {};
  previewEnvironment_.ambient.intensity = 0.35f;
  previewEnvironment_.ambient.useSkyColor = true;
  previewEnvironment_.ambient.skyBlend = 0.8f;
  previewEnvironment_.directional.enabled = false;
  previewEnvironment_.sky.enabled = true;
  previewEnvironment_.sky.mode = SkyMode::Cubemap;
  previewEnvironment_.sky.skyAsset = "DefaultSky";
  previewEnvironment_.sky.exposure = 1.0f;
  previewEnvironment_.sky.reflectionIntensity = 1.0f;
  previewEnvironment_.screenSpaceReflections.enabled = false;
  previewEnvironment_.surfaceLightmaps.enabled = true;

  previewLighting_ = {};
  RENDER3D::LIGHTING::SceneLightProvider key{};
  key.id = "ModelMaterialPreview.Key";
  key.name = "Movable Key";
  key.enabled = true;
  key.type = RENDER3D::LIGHTING::SceneLightType::Point;
  key.position = {2.5f, 3.5f, -2.5f};
  key.color = {1.0f, 0.96f, 0.9f};
  key.intensity = 18.0f;
  key.range = 14.0f;
  key.priority = 100;
  previewLighting_.lights.push_back(std::move(key));
  previewLightingInitialized_ = true;
}

} // namespace HIKARI::EDITOR
