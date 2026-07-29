#include "Editor/Environment/HIKARI_EnvironmentPanel.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>
#endif

namespace HIKARI {

using namespace EDITOR::ENVIRONMENT_PANEL;

#if defined(HIKARI_WITH_EDITOR)
namespace {
constexpr const char *kDefaultGlobalPostProfileId = "Ani";

size_t CountUploadablePointLights(const SceneEnvironment &environment) {
  size_t count = 0;
  for (const PointLight &pointLight : environment.pointLights) {
    if (pointLight.enabled && pointLight.range > 0.0f) {
      ++count;
    }
  }
  return count;
}

constexpr float kPi = 3.14159265358979323846f;

float RadToDeg(float radians) { return radians * 180.0f / kPi; }

float DegToRad(float degrees) { return degrees * kPi / 180.0f; }

MATH::Vec3 DirectionFromYawPitch(float yawDeg, float pitchDeg) {
  const float yaw = DegToRad(yawDeg);
  const float pitch = DegToRad(std::clamp(pitchDeg, -89.0f, 89.0f));
  const float cp = std::cos(pitch);
  return MATH::Normalize(
      MATH::Vec3{std::sin(yaw) * cp, -std::sin(pitch), std::cos(yaw) * cp});
}

void YawPitchFromDirection(const MATH::Vec3 &direction, float &yawDeg,
                           float &pitchDeg) {
  const MATH::Vec3 dir = MATH::Normalize(direction);
  yawDeg = RadToDeg(std::atan2(dir.x, dir.z));
  pitchDeg = RadToDeg(std::asin(std::clamp(-dir.y, -1.0f, 1.0f)));
}

} // namespace

EnvironmentPanelResult
EnvironmentPanel::Draw(SceneEnvironment &environment,
                       const SKYRENDERER::SkyRendererDebugState *,
                       const AssetRegistry *assetRegistry,
                       const AssetDatabase *assetDatabase) const {
  if (!ImGui::Begin("Environment")) {
    ImGui::End();
    return {};
  }

  // UI
  // 髯ｷ闌ｨ・ｽ・ｨ髣厄ｽｴ髦ｮ蜷ｶ繝ｻ鬩搾ｽｱ繝ｻ・ｨ鬯ｮ・ｮ郢晢ｽｻ霎ｯ謌奇｣ｰ蜍滂ｽｾ魃会ｽｽ螳夲ｽｱ閧ｲ・｢謇假ｽｽ・ｼ郢晢ｽｻ繝ｻ・ｰ驍ｵ・ｲ繝ｻ・｣reset
  // 驛｢・ｧ郢晢ｽｻ郢晢ｽｻ髯具ｽｻ驍・ｽｲ隴ｯ繝ｻ謚・ｫ帛･・ｽｽ繧会ｽｸ・ｺ繝ｻ・ｾ驍ｵ・ｺ繝ｻ・ｨ驛｢・ｧ遶丞｣ｺﾂ・ｻ髫ｶﾂ隲幢ｽｷ郢晢ｽｻ驍ｵ・ｺ陷ｷ・ｶ繝ｻ迢暦ｽｸ・ｲ郢晢ｽｻ
  // 鬩搾ｽｱ繝ｻ・ｨ鬯ｮ・ｮ郢晢ｽｻ・つ繝ｻ・､驍ｵ・ｺ繝ｻ・ｯ髯ｷ螂・ｽｽ・ｳ髫ｴ蠑ｱ・・ｫ翫・runtime
  // 驍ｵ・ｺ繝ｻ・ｸ髯ｷ・ｿ髢ｧ・ｴ闕ｳ蜊・ｽｸ・ｺ陷会ｽｱ・つ遶擾ｽｬ繝ｻ・ｩ繝ｻ・ｳ鬩肴得・ｽ・ｰ鬮ｫ・ｪ繝ｻ・ｺ髫ｴ繝ｻ・ｽ・ｭ驍ｵ・ｺ繝ｻ・ｯ
  // log
  // 驍ｵ・ｺ繝ｻ・ｸ鬯ｨ・ｾ郢晢ｽｻ遯ｶ・ｲ驍ｵ・ｺ陷ｷ・ｶ・つ郢晢ｽｻ
  const SceneEnvironment beforeEdit = environment;
  bool openLightingBakeRequested = false;

  ImGui::SeparatorText("Scene Environment");
  const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
      RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);
  ImGui::TextDisabled(
      "Runtime: %s | %s | Errors %u",
      RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
      RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
      runtimeSnapshot.recentRenderErrorCount);
  if (runtimeSnapshot.recentRenderErrorCount > 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Check log");
  }

  if (ImGui::TreeNodeEx("Quick Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat("Sun Intensity", &environment.directional.intensity, 0.01f,
                     0.0f, 20.0f);
    ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f,
                     0.0f, 10.0f);
    ImGui::DragFloat("Fog Amount", &environment.fog.density, 0.001f, 0.0f,
                     1.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Presets")) {
    const char *presets[] = {
        "Default",  "Bright Day",    "Sunset",      "Night",
        "Overcast", "Stylized Blue", "Warm Indoor",
    };
    const int presetCount =
        static_cast<int>(sizeof(presets) / sizeof(presets[0]));
    for (int i = 0; i < presetCount; ++i) {
      ImGui::PushID(i);
      if (ImGui::Button(presets[i])) {
        ApplyEnvironmentPreset(environment, i);
      }
      if ((i % 3) != 2 && (i + 1) < presetCount) {
        ImGui::SameLine();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
    ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f,
                     0.0f, 10.0f);
    ImGui::Checkbox("Use Sky Color", &environment.ambient.useSkyColor);
    ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend, 0.01f,
                     0.0f, 1.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Directional Enabled", &environment.directional.enabled);
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    YawPitchFromDirection(environment.directional.direction, yawDeg, pitchDeg);
    bool angleChanged = false;
    angleChanged |= ImGui::DragFloat("Sun Yaw", &yawDeg, 0.5f, -180.0f, 180.0f);
    angleChanged |=
        ImGui::DragFloat("Sun Pitch", &pitchDeg, 0.5f, -89.0f, 89.0f);
    if (angleChanged) {
      environment.directional.direction =
          DirectionFromYawPitch(yawDeg, pitchDeg);
    }
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

  if (ImGui::TreeNodeEx("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    constexpr size_t kMaxUploadedPointLights = 8u;
    const size_t uploadableCount = CountUploadablePointLights(environment);
    const size_t uploadedPreviewCount =
        uploadableCount < kMaxUploadedPointLights ? uploadableCount
                                                  : kMaxUploadedPointLights;
    ImGui::Text("Total: %zu  Uploadable: %zu / %zu",
                environment.pointLights.size(), uploadedPreviewCount,
                kMaxUploadedPointLights);
    if (uploadableCount > kMaxUploadedPointLights) {
      ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                         "Only first 8 enabled point lights are uploaded.");
    }
    if (ImGui::Button("Add Point Light")) {
      environment.pointLights.push_back(PointLight{});
    }
    for (size_t i = 0; i < environment.pointLights.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      PointLight &pointLight = environment.pointLights[i];
      if (ImGui::TreeNode("PointLight", "Point Light %zu", i)) {
        ImGui::Checkbox("Enabled", &pointLight.enabled);
        ImGui::DragFloat3("Position", &pointLight.position.x, 0.02f);
        ImGui::DragFloat("Range", &pointLight.range, 0.05f, 0.0f, 100.0f);
        ImGui::ColorEdit3("Color", &pointLight.color.x);
        ImGui::DragFloat("Intensity", &pointLight.intensity, 0.01f, 0.0f,
                         20.0f);

        if (ImGui::Button("Duplicate")) {
          environment.pointLights.insert(environment.pointLights.begin() +
                                             static_cast<long long>(i + 1),
                                         pointLight);
          ImGui::TreePop();
          ImGui::PopID();
          break;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
          environment.pointLights.erase(environment.pointLights.begin() +
                                        static_cast<long long>(i));
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

  if (ImGui::TreeNode("Specular")) {
    ImGui::DragFloat("Specular Intensity", &environment.specularIntensity,
                     0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Specular Power", &environment.specularPower, 1.0f, 1.0f,
                     256.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Sky")) {
    ImGui::Checkbox("Sky Enabled", &environment.sky.enabled);
    int skyMode = static_cast<int>(environment.sky.mode);
    const char *skyModeNames[] = {"None", "Gradient", "Cubemap", "Texture2D"};
    if (ImGui::Combo("Sky Mode", &skyMode, skyModeNames,
                     static_cast<int>(std::size(skyModeNames)))) {
      environment.sky.mode = static_cast<SkyMode>(std::clamp(skyMode, 0, 3));
    }
    DrawSkyAssetPicker(assetRegistry, assetDatabase, environment.sky.skyAsset);
    ImGui::DragFloat("Sky Scale", &environment.sky.scale, 0.01f, 0.0001f,
                     1000.0f);
    ImGui::DragFloat("Sky Yaw", &environment.sky.yaw, 0.01f);
    ImGui::DragFloat("Sky Exposure", &environment.sky.exposure, 0.01f, 0.0f,
                     16.0f);
    ImGui::ColorEdit3("Sky Tint", &environment.sky.tint.x);
    ImGui::Checkbox("Follow Camera", &environment.sky.followCamera);

    if (environment.sky.mode == SkyMode::Gradient ||
        environment.sky.mode == SkyMode::Cubemap) {
      if (ImGui::TreeNode("Gradient Fallback")) {
        ImGui::ColorEdit3("Zenith Color", &environment.sky.zenithColor.x);
        ImGui::ColorEdit3("Horizon Color", &environment.sky.horizonColor.x);
        ImGui::ColorEdit3("Ground Color", &environment.sky.groundColor.x);
        ImGui::DragFloat("Horizon Power", &environment.sky.horizonPower, 0.01f,
                         0.01f, 8.0f);
        ImGui::TreePop();
      }
    }
    if (ImGui::TreeNode("Sun Disk")) {
      ImGui::Checkbox("Show Sun Disk", &environment.sky.showSunDisk);
      ImGui::DragFloat("Sun Disk Intensity", &environment.sky.sunDiskIntensity,
                       0.01f, 0.0f, 32.0f);
      ImGui::DragFloat("Sun Disk Size", &environment.sky.sunDiskSize, 0.001f,
                       0.001f, 0.5f);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Environment Output")) {
      ImGui::DragFloat("Ambient From Sky", &environment.sky.ambientFromSky,
                       0.01f, 0.0f, 8.0f);
      ImGui::DragFloat("Reflection Intensity",
                       &environment.sky.reflectionIntensity, 0.01f, 0.0f, 8.0f);
      ImGui::Checkbox("Use Sky Color For Ambient",
                      &environment.ambient.useSkyColor);
      ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend,
                       0.01f, 0.0f, 1.0f);
      ImGui::Checkbox("Use Sky Horizon For Fog",
                      &environment.fog.useSkyHorizonColor);
      if (ImGui::Button("Apply Horizon To Fog")) {
        environment.fog.color = environment.sky.horizonColor;
      }
      ImGui::TreePop();
    }
    if (environment.sky.mode == SkyMode::Cubemap) {
      ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f),
                         "Cubemap mode requires DDS cubemap texture.");
      ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f),
                         "PNG/JPG panorama should use Texture2D mode.");
    }
    ImGui::Checkbox("Show Sky Debug Texture",
                    &environment.sky.showDebugTexture);
    ImGui::TextDisabled(
        "Runtime Sky: %s",
        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot));
    ImGui::TextDisabled(
        "Runtime IBL: %s",
        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot));
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Lighting Objects")) {
    ImGui::TextWrapped(
        "Reflection probes and irradiance volumes are scene objects. "
        "Create them from Add > Lighting, then edit their components "
        "in the Inspector.");
    if (ImGui::Button("Open Lighting Bake Tool")) {
      openLightingBakeRequested = true;
    }
    ImGui::TextDisabled(
        "Primary runtime probe: %s",
        RENDER3D::DIAGNOSTICS::ResolveReflectionProbeSummaryLabel(
            runtimeSnapshot));
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Fog")) {
    ImGui::Checkbox("Fog Enabled", &environment.fog.enabled);
    ImGui::Checkbox("Volumetric Fog", &environment.fog.volumetric);
    ImGui::ColorEdit3("Fog Color", &environment.fog.color.x);
    ImGui::DragFloat("Density", &environment.fog.density, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Start Distance", &environment.fog.startDistance, 0.1f,
                     0.0f, 500.0f);
    ImGui::DragFloat("End Distance", &environment.fog.endDistance, 0.1f, 0.1f,
                     1000.0f);
    ImGui::DragFloat("Height Falloff", &environment.fog.heightFalloff, 0.001f,
                     0.0f, 2.0f);
    ImGui::SliderFloat("Anisotropy", &environment.fog.anisotropy, -0.8f, 0.8f);
    ImGui::SliderFloat("Temporal Weight", &environment.fog.temporalWeight, 0.0f,
                       0.98f);
    ImGui::Checkbox("Use Sky Horizon Color",
                    &environment.fog.useSkyHorizonColor);
    if (ImGui::Button("Set Fog Color From Sky Horizon")) {
      environment.fog.color = environment.sky.horizonColor;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Debug")) {
    ImGui::Checkbox("Show Light Debug", &environment.showLightDebug);
    ImGui::Checkbox("Show Point Light Markers",
                    &environment.showPointLightMarkers);
    ImGui::Checkbox("Show Sky Debug Info", &environment.showSkyDebugInfo);

    ImGui::TextDisabled(
        "Runtime: %s | %s | Errors %u",
        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
        runtimeSnapshot.recentRenderErrorCount);
    if (runtimeSnapshot.recentRenderErrorCount > 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                         "Render diagnostics contain recent errors.");
    }

    // Debug dump 驍ｵ・ｺ繝ｻ・ｯ scene dirty
    // 驍ｵ・ｺ繝ｻ・ｫ驍ｵ・ｺ陷会ｽｱ遶企・・ｸ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
    if (ImGui::Button("Dump Environment Diagnostics")) {
      RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshot("EnvironmentPanel",
                                                    &environment);
    }
    ImGui::TreePop();
  }

  if (ImGui::Button("Reset Environment Defaults")) {
    environment = SceneEnvironment{};
    NormalizeDirectionalLight(environment.directional);
  }

  const bool changed =
      !EDITOR::ENVIRONMENT_EDIT::AreSceneEnvironmentValuesEqual(beforeEdit,
                                                                environment);
  const bool editActive =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsAnyItemActive();
  ImGui::End();
  return EnvironmentPanelResult{
      changed,
      openLightingBakeRequested,
      editActive};
}
#else
EnvironmentPanelResult
EnvironmentPanel::Draw(SceneEnvironment &,
                       const SKYRENDERER::SkyRendererDebugState *,
                       const AssetRegistry *, const AssetDatabase *) const {
  return {};
}
#endif

} // namespace HIKARI
