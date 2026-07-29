#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "Render3D/Lighting/HIKARI_LocalLightFrame.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"
#include "imgui.h"

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

namespace {

float RadiansToDegrees(float radians) {
  return radians * 180.0f / std::numbers::pi_v<float>;
}

float DegreesToRadians(float degrees) {
  return degrees * std::numbers::pi_v<float> / 180.0f;
}

MATH::Vec3 DirectionFromYawPitch(float yawDegrees, float pitchDegrees) {
  const float yaw = DegreesToRadians(yawDegrees);
  const float pitch =
      DegreesToRadians(std::clamp(pitchDegrees, -89.0f, 89.0f));
  const float cosPitch = std::cos(pitch);
  return MATH::Normalize(MATH::Vec3{
      std::sin(yaw) * cosPitch,
      -std::sin(pitch),
      std::cos(yaw) * cosPitch,
  });
}

void YawPitchFromDirection(const MATH::Vec3 &direction, float &yawDegrees,
                           float &pitchDegrees) {
  const MATH::Vec3 normalized = MATH::Normalize(direction);
  yawDegrees = RadiansToDegrees(std::atan2(normalized.x, normalized.z));
  pitchDegrees = RadiansToDegrees(
      std::asin(std::clamp(-normalized.y, -1.0f, 1.0f)));
}

void DrawDirectionControls(const char *yawLabel, const char *pitchLabel,
                           MATH::Vec3 &direction) {
  float yawDegrees = 0.0f;
  float pitchDegrees = 0.0f;
  YawPitchFromDirection(direction, yawDegrees, pitchDegrees);
  bool angleChanged = false;
  angleChanged |=
      ImGui::DragFloat(yawLabel, &yawDegrees, 0.5f, -180.0f, 180.0f);
  angleChanged |=
      ImGui::DragFloat(pitchLabel, &pitchDegrees, 0.5f, -89.0f, 89.0f);
  if (angleChanged) {
    direction = DirectionFromYawPitch(yawDegrees, pitchDegrees);
  }
}

void DrawPointLights(SceneEnvironment &environment) {
  if (!ImGui::TreeNodeEx("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  if (ImGui::Button("Add Point Light")) {
    environment.pointLights.push_back(PointLight{});
  }

  for (size_t index = 0; index < environment.pointLights.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    PointLight &light = environment.pointLights[index];
    if (ImGui::TreeNode("PointLight", "Point Light %zu", index)) {
      ImGui::Checkbox("Enabled", &light.enabled);
      ImGui::DragFloat3("Position", &light.position.x, 0.02f);
      ImGui::DragFloat("Range", &light.range, 0.05f, 0.0f, 100.0f);
      ImGui::ColorEdit3("Color", &light.color.x);
      ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
      ImGui::DragInt("Priority", &light.priority, 1.0f, -16, 16);

      if (ImGui::Button("Duplicate")) {
        environment.pointLights.insert(
            environment.pointLights.begin() +
                static_cast<std::ptrdiff_t>(index + 1u),
            light);
        ImGui::TreePop();
        ImGui::PopID();
        break;
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove")) {
        environment.pointLights.erase(
            environment.pointLights.begin() +
            static_cast<std::ptrdiff_t>(index));
        ImGui::TreePop();
        ImGui::PopID();
        break;
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::TreePop();
}

void DrawSpotLights(SceneEnvironment &environment) {
  if (!ImGui::TreeNodeEx("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  if (ImGui::Button("Add Spot Light")) {
    environment.spotLights.push_back(SpotLight{});
  }

  for (size_t index = 0; index < environment.spotLights.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    SpotLight &light = environment.spotLights[index];
    if (ImGui::TreeNode("SpotLight", "Spot Light %zu", index)) {
      ImGui::Checkbox("Enabled", &light.enabled);
      ImGui::DragFloat3("Position", &light.position.x, 0.02f);
      ImGui::DragFloat("Range", &light.range, 0.05f, 0.0f, 200.0f);
      DrawDirectionControls("Yaw", "Pitch", light.direction);
      if (ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f,
                            1.0f)) {
        light.direction =
            RENDER3D::LIGHTING::ResolveSpotLightDirection(light);
      }
      ImGui::DragFloat("Inner Angle", &light.innerConeAngleDegrees, 0.25f,
                       0.0f, 88.9f, "%.1f deg");
      ImGui::DragFloat("Outer Angle", &light.outerConeAngleDegrees, 0.25f,
                       0.1f, 89.0f, "%.1f deg");
      ImGui::DragFloat("Edge Falloff", &light.angularFalloff, 0.05f, 0.1f,
                       16.0f);
      ImGui::ColorEdit3("Color", &light.color.x);
      ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);
      ImGui::DragInt("Priority", &light.priority, 1.0f, -16, 16);
      RENDER3D::LIGHTING::SanitizeSpotLight(light);

      if (ImGui::Button("Duplicate")) {
        environment.spotLights.insert(
            environment.spotLights.begin() +
                static_cast<std::ptrdiff_t>(index + 1u),
            light);
        ImGui::TreePop();
        ImGui::PopID();
        break;
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove")) {
        environment.spotLights.erase(
            environment.spotLights.begin() +
            static_cast<std::ptrdiff_t>(index));
        ImGui::TreePop();
        ImGui::PopID();
        break;
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::TreePop();
}

} // namespace

void DrawEnvironmentLightingControls(SceneEnvironment &environment) {
  if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
    ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f,
                     0.0f, 10.0f);
    ImGui::Checkbox("Use Sky Color", &environment.ambient.useSkyColor);
    ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend, 0.01f,
                     0.0f, 1.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Directional Light",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Directional Enabled", &environment.directional.enabled);
    DrawDirectionControls("Sun Yaw", "Sun Pitch",
                          environment.directional.direction);
    if (ImGui::DragFloat3("Direction", &environment.directional.direction.x,
                          0.01f, -1.0f, 1.0f)) {
      NormalizeDirectionalLight(environment.directional);
    }
    NormalizeDirectionalLight(environment.directional);
    ImGui::ColorEdit3("Color", &environment.directional.color.x);
    ImGui::DragFloat("Intensity", &environment.directional.intensity, 0.01f,
                     0.0f, 20.0f);
    ImGui::TreePop();
  }

  const RENDER3D::LIGHTING::LocalLightFrame localLightFrame =
      RENDER3D::LIGHTING::BuildLocalLightFrame(environment);
  ImGui::TextDisabled(
      "Local Light Budget: %u / %u  (Point %u, Spot %u)",
      localLightFrame.uploadedCount,
      RENDER3D::LIGHTING::kLocalLightFrameCapacity,
      localLightFrame.uploadedPointCount,
      localLightFrame.uploadedSpotCount);
  if (localLightFrame.clampedCount > 0u) {
    ImGui::TextColored(
        ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
        "%u light(s) omitted. Raise Priority to select important lights.",
        localLightFrame.clampedCount);
  }

  DrawPointLights(environment);
  DrawSpotLights(environment);
}

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL

#endif
