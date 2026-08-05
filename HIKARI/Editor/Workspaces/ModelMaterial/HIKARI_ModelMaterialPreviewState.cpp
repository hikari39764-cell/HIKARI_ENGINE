#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::SaveActiveCameraState() {
  if (authoringMode_ == AuthoringMode::ModelMaterials &&
      modelGuid_.IsValid()) {
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
  const Bounds &bounds = GetActivePreviewScene().GetBounds();
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
  if (authoringMode_ == AuthoringMode::ModelMaterials &&
      modelGuid_.IsValid()) {
    cameraStateStore_.Erase(EditorWorkspaceId::ModelMaterial, modelGuid_);
  }
  FitPreviewCamera();
}

void ModelMaterialWorkspaceController::ResetPreviewKeyLight() {
  if (previewLighting_.lights.empty()) {
    return;
  }

  MATH::Vec3 center{};
  float radius = 3.5f;
  const Bounds &bounds = GetActivePreviewScene().GetBounds();
  if (BOUNDS::IsUsable(bounds)) {
    center = (bounds.min + bounds.max) * 0.5f;
    radius = (std::max)(
        MATH::Length(bounds.max - bounds.min) * 0.5f, 0.5f);
  }

  const size_t lightIndex =
      selectedPreviewLightIndex_.has_value() &&
              *selectedPreviewLightIndex_ < previewLighting_.lights.size()
          ? *selectedPreviewLightIndex_
          : 0u;
  auto &key = previewLighting_.lights[lightIndex];
  key.enabled = true;
  key.position =
      center + MATH::Vec3{radius * 0.8f, radius * 1.15f, -radius * 0.8f};
  key.color = {1.0f, 0.93f, 0.82f};
  if (key.type == RENDER3D::LIGHTING::SceneLightType::Directional) {
    key.direction = MATH::Normalize(center - key.position);
    key.intensity = 3.0f;
  } else {
    key.intensity = 18.0f;
    key.range = (std::max)(radius * 4.0f, 14.0f);
  }
}

void ModelMaterialWorkspaceController::AddPreviewLight(
    RENDER3D::LIGHTING::SceneLightType type) {
  previewLightRangeDragActive_ = false;
  InitializePreviewLighting();
  RENDER3D::LIGHTING::SceneLightProvider light{};
  light.id = "ModelMaterialStudioInspect.Light." +
             std::to_string(previewLightIdCounter_++);
  light.name = type == RENDER3D::LIGHTING::SceneLightType::Directional
                   ? "Directional Light"
                   : "Point Light";
  light.enabled = true;
  light.type = type;
  light.color = {1.0f, 0.96f, 0.90f};
  light.priority = 100;
  previewLighting_.lights.push_back(std::move(light));
  selectedPreviewLightIndex_ = previewLighting_.lights.size() - 1u;
  ResetPreviewKeyLight();
  previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
}

void ModelMaterialWorkspaceController::DeleteSelectedPreviewLight() {
  previewLightRangeDragActive_ = false;
  if (!selectedPreviewLightIndex_.has_value() ||
      *selectedPreviewLightIndex_ >= previewLighting_.lights.size()) {
    return;
  }
  previewLighting_.lights.erase(
      previewLighting_.lights.begin() +
      static_cast<std::ptrdiff_t>(*selectedPreviewLightIndex_));
  if (previewLighting_.lights.empty()) {
    selectedPreviewLightIndex_.reset();
  } else {
    selectedPreviewLightIndex_ = (std::min)(
        *selectedPreviewLightIndex_, previewLighting_.lights.size() - 1u);
  }
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
  previewEnvironment_.sky.reflectionIntensity = 0.45f;
  previewEnvironment_.screenSpaceReflections.enabled = false;
  previewEnvironment_.surfaceLightmaps.enabled = false;

  previewLighting_ = {};
  RENDER3D::LIGHTING::SceneLightProvider key{};
  key.id = "ModelMaterialStudioInspect.Key";
  key.name = "Studio Movable Key";
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
