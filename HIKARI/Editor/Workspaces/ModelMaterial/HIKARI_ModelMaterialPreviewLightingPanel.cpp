#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawPreviewLightingPanel(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!previewLightingPanelOpen_) {
    return;
  }
  if (!ImGui::Begin("Preview Lighting###ModelMaterial/PreviewLighting",
                    &previewLightingPanelOpen_)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled(
      "Studio Inspect light rig. Scene Match and Runtime Asset Lighting use "
      "the active scene lights.");
  if (ImGui::CollapsingHeader("Environment Presets",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto applyPreset = [this](float ambient, float skyExposure,
                                    float reflection, float keyIntensity) {
      previewEnvironment_.ambient.intensity = ambient;
      previewEnvironment_.sky.exposure = skyExposure;
      previewEnvironment_.sky.reflectionIntensity = reflection;
      if (!previewLighting_.lights.empty()) {
        previewLighting_.lights.front().intensity = keyIntensity;
      }
      MarkPreviewLightingChanged();
    };
    if (ImGui::Button("Dark Studio")) {
      applyPreset(0.15f, 0.40f, 0.15f, 2.2f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Neutral Studio")) {
      applyPreset(0.35f, 1.00f, 0.45f, 3.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Bright Studio")) {
      applyPreset(0.55f, 1.60f, 0.80f, 4.2f);
    }
    if (ImGui::Button("Reset Key Light")) {
      ResetPreviewKeyLight();
    }
  }
  if (!previewLighting_.lights.empty()) {
    const size_t lightIndex =
        selectedPreviewLightIndex_.has_value() &&
                *selectedPreviewLightIndex_ < previewLighting_.lights.size()
            ? *selectedPreviewLightIndex_
            : 0u;
    auto &light = previewLighting_.lights[lightIndex];
    bool changed = false;
    ImGui::SeparatorText("Selected Light");
    ImGui::TextUnformatted(light.name.c_str());
    changed = ImGui::Checkbox("Enabled", &light.enabled) || changed;
    changed = ImGui::ColorEdit3("Color", &light.color.x) || changed;
    changed = ImGui::DragFloat("Intensity", &light.intensity, 0.05f,
                               0.0f, 64.0f) || changed;
    if (light.type == RENDER3D::LIGHTING::SceneLightType::Point) {
      changed = ImGui::DragFloat("Range", &light.range, 0.10f, 0.1f,
                                 200.0f) || changed;
      changed = ImGui::DragFloat3("Position", &light.position.x, 0.05f) ||
                changed;
    } else if (light.type ==
               RENDER3D::LIGHTING::SceneLightType::Directional) {
      MATH::Vec3 direction = MATH::Length(light.direction) > 1.0e-5f
                                 ? MATH::Normalize(light.direction)
                                 : MATH::Vec3{0.0f, -1.0f, 0.0f};
      float yawDegrees = std::atan2(direction.x, direction.z) *
                         57.2957795131f;
      float pitchDegrees = std::asin(
          std::clamp(direction.y, -1.0f, 1.0f)) * 57.2957795131f;
      bool directionChanged = false;
      directionChanged = ImGui::DragFloat(
          "Yaw (deg)", &yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f") ||
          directionChanged;
      directionChanged = ImGui::DragFloat(
          "Pitch (deg)", &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f") ||
          directionChanged;
      if (directionChanged) {
        const float yawRad = yawDegrees * 0.0174532925f;
        const float pitchRad = pitchDegrees * 0.0174532925f;
        const float cosinePitch = std::cos(pitchRad);
        light.direction = MATH::Normalize({
            std::sin(yawRad) * cosinePitch,
            std::sin(pitchRad),
            std::cos(yawRad) * cosinePitch,
        });
        changed = true;
      }
    }
    ImGui::SeparatorText("Key Light Sync");
    const SceneEnvironment &environment = scene.GetSceneEnvironment();
    if (environment.directional.enabled &&
        MATH::Length(environment.directional.direction) > 1.0e-5f) {
      if (ImGui::Button("Sync Key with Runtime Directional")) {
        light.type = RENDER3D::LIGHTING::SceneLightType::Directional;
        light.direction = MATH::Normalize(
            environment.directional.direction);
        light.color = environment.directional.color;
        light.intensity = environment.directional.intensity;
        changed = true;
      }
    } else {
      ImGui::TextDisabled(
          "Scene has no enabled directional light to sync.");
    }
    if (changed) {
      MarkPreviewLightingChanged();
    }
  }
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
