#include "Editor/Environment/HIKARI_EnvironmentPanel.h"
#include "Core/Text/HIKARI_Localization.h"
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
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#endif

namespace HIKARI {

using namespace EDITOR::ENVIRONMENT_PANEL;

#if defined(HIKARI_WITH_EDITOR)
namespace {
constexpr const char *kDefaultGlobalPostProfileId = "Ani";

} // namespace

EnvironmentPanelResult
EnvironmentPanel::Draw(SceneEnvironment &environment,
                       const SKYRENDERER::SkyRendererDebugState *,
                       const AssetRegistry *assetRegistry,
                       const AssetDatabase *assetDatabase) const {
  if (!ImGui::Begin(
          TEXT::LocalizeWindow(
              "ui.window.environment", "Environment").c_str())) {
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

  ImGui::SeparatorText(
      TEXT::Localize("ui.environment.sceneEnvironment").c_str());
  const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
      RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);
  ImGui::TextDisabled(
      TEXT::Localize("ui.environment.runtimeFormat").c_str(),
      RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
      RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
      runtimeSnapshot.recentRenderErrorCount);
  if (runtimeSnapshot.recentRenderErrorCount > 0) {
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
        "%s",
        TEXT::Localize("ui.environment.checkLog").c_str());
  }

  if (ImGui::TreeNodeEx(
          TEXT::Localize("ui.environment.quickControls").c_str(),
          ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.ambientIntensity").c_str(),
        &environment.ambient.intensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.fogAmount").c_str(),
        &environment.fog.density, 0.001f, 0.0f, 1.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNode(
          TEXT::Localize("ui.environment.presets").c_str())) {
    const char *presetKeys[] = {
        "ui.environment.preset.default",
        "ui.environment.preset.brightDay",
        "ui.environment.preset.sunset",
        "ui.environment.preset.night",
        "ui.environment.preset.overcast",
        "ui.environment.preset.stylizedBlue",
        "ui.environment.preset.warmIndoor",
    };
    const int presetCount =
        static_cast<int>(sizeof(presetKeys) / sizeof(presetKeys[0]));
    for (int i = 0; i < presetCount; ++i) {
      ImGui::PushID(i);
      if (ImGui::Button(TEXT::Localize(presetKeys[i]).c_str())) {
        ApplyEnvironmentPreset(environment, i);
      }
      if ((i % 3) != 2 && (i + 1) < presetCount) {
        ImGui::SameLine();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  DrawEnvironmentLightingControls(environment);

  if (ImGui::TreeNode(
          TEXT::Localize("ui.environment.specular").c_str())) {
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.specularIntensity").c_str(),
        &environment.specularIntensity, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.specularPower").c_str(),
        &environment.specularPower, 1.0f, 1.0f, 256.0f);
    ImGui::TreePop();
  }

  if (ImGui::TreeNode(TEXT::Localize("ui.environment.sky").c_str())) {
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.skyEnabled").c_str(),
        &environment.sky.enabled);
    int skyMode = static_cast<int>(environment.sky.mode);
    const std::array<std::string, 4> skyModeNames{
        TEXT::Localize("ui.environment.skyModeNone"),
        TEXT::Localize("ui.environment.skyModeGradient"),
        TEXT::Localize("ui.environment.skyModeCubemap"),
        TEXT::Localize("ui.environment.skyModeTexture2D"),
    };
    const char *skyModeNamePtrs[4]{};
    for (size_t index = 0; index < skyModeNames.size(); ++index) {
        skyModeNamePtrs[index] = skyModeNames[index].c_str();
    };
    if (ImGui::Combo(
            TEXT::Localize("ui.environment.skyMode").c_str(),
            &skyMode, skyModeNamePtrs,
                     static_cast<int>(skyModeNames.size()))) {
      environment.sky.mode = static_cast<SkyMode>(std::clamp(skyMode, 0, 3));
    }
    DrawSkyAssetPicker(assetRegistry, assetDatabase, environment.sky.skyAsset);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.skyScale").c_str(),
        &environment.sky.scale, 0.01f, 0.0001f, 1000.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.skyYaw").c_str(),
        &environment.sky.yaw, 0.01f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.skyIntensity").c_str(),
        &environment.sky.exposure, 0.01f, 0.0f, 16.0f);
    ImGui::ColorEdit3(
        TEXT::Localize("ui.environment.skyTint").c_str(),
        &environment.sky.tint.x);
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.followCamera").c_str(),
        &environment.sky.followCamera);

    if (environment.sky.mode == SkyMode::Gradient ||
        environment.sky.mode == SkyMode::Cubemap) {
      if (ImGui::TreeNode(
              TEXT::Localize(
                  "ui.environment.gradientFallback").c_str())) {
        ImGui::ColorEdit3(
            TEXT::Localize("ui.environment.zenithColor").c_str(),
            &environment.sky.zenithColor.x);
        ImGui::ColorEdit3(
            TEXT::Localize("ui.environment.horizonColor").c_str(),
            &environment.sky.horizonColor.x);
        ImGui::ColorEdit3(
            TEXT::Localize("ui.environment.groundColor").c_str(),
            &environment.sky.groundColor.x);
        ImGui::DragFloat(
            TEXT::Localize("ui.environment.horizonPower").c_str(),
            &environment.sky.horizonPower, 0.01f, 0.01f, 8.0f);
        ImGui::TreePop();
      }
    }
    if (ImGui::TreeNode(
            TEXT::Localize("ui.environment.sunDisk").c_str())) {
      ImGui::Checkbox(
          TEXT::Localize("ui.environment.showSunDisk").c_str(),
          &environment.sky.showSunDisk);
      ImGui::DragFloat(
          TEXT::Localize("ui.environment.sunDiskIntensity").c_str(),
          &environment.sky.sunDiskIntensity, 0.01f, 0.0f, 32.0f);
      ImGui::DragFloat(
          TEXT::Localize("ui.environment.sunDiskSize").c_str(),
          &environment.sky.sunDiskSize, 0.001f, 0.001f, 0.5f);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode(
            TEXT::Localize(
                "ui.environment.environmentOutput").c_str())) {
      ImGui::DragFloat(
          TEXT::Localize("ui.environment.ambientFromSky").c_str(),
          &environment.sky.ambientFromSky, 0.01f, 0.0f, 8.0f);
      ImGui::DragFloat(
          TEXT::Localize("ui.environment.reflectionIntensity").c_str(),
          &environment.sky.reflectionIntensity, 0.01f, 0.0f, 8.0f);
      ImGui::Checkbox(
          TEXT::Localize(
              "ui.environment.useSkyColorForAmbient").c_str(),
          &environment.ambient.useSkyColor);
      ImGui::DragFloat(
          TEXT::Localize("ui.environment.skyAmbientBlend").c_str(),
          &environment.ambient.skyBlend, 0.01f, 0.0f, 1.0f);
      ImGui::Checkbox(
          TEXT::Localize(
              "ui.environment.useSkyHorizonForFog").c_str(),
          &environment.fog.useSkyHorizonColor);
      if (ImGui::Button(
              TEXT::Localize(
                  "ui.environment.applyHorizonToFog").c_str())) {
        environment.fog.color = environment.sky.horizonColor;
      }
      ImGui::TreePop();
    }
    if (environment.sky.mode == SkyMode::Cubemap) {
      ImGui::TextColored(
          ImVec4(0.75f, 0.85f, 1.0f, 1.0f),
          "%s",
          TEXT::Localize(
              "ui.environment.cubemapRequiresDds").c_str());
      ImGui::TextColored(
          ImVec4(0.75f, 0.85f, 1.0f, 1.0f),
          "%s",
          TEXT::Localize(
              "ui.environment.panoramaUseTexture2D").c_str());
    }
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.showSkyDebugTexture").c_str(),
        &environment.sky.showDebugTexture);
    ImGui::TextDisabled(
        TEXT::Localize("ui.environment.runtimeSkyFormat").c_str(),
        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot));
    ImGui::TextDisabled(
        TEXT::Localize("ui.environment.runtimeIblFormat").c_str(),
        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot));
    ImGui::TreePop();
  }

  if (ImGui::TreeNode(
          TEXT::Localize("ui.environment.lightingObjects").c_str())) {
    ImGui::TextWrapped(
        "%s",
        TEXT::Localize("ui.environment.lightingObjectsHint").c_str());
    if (ImGui::Button(
            TEXT::Localize(
                "ui.environment.openLightingBakeTool").c_str())) {
      openLightingBakeRequested = true;
    }
    ImGui::TextDisabled(
        TEXT::Localize("ui.environment.primaryProbeFormat").c_str(),
        RENDER3D::DIAGNOSTICS::ResolveReflectionProbeSummaryLabel(
            runtimeSnapshot));
    ImGui::TreePop();
  }

  if (ImGui::TreeNode(TEXT::Localize("ui.environment.fog").c_str())) {
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.fogEnabled").c_str(),
        &environment.fog.enabled);
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.volumetricFog").c_str(),
        &environment.fog.volumetric);
    ImGui::ColorEdit3(
        TEXT::Localize("ui.environment.fogColor").c_str(),
        &environment.fog.color.x);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.density").c_str(),
        &environment.fog.density, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.startDistance").c_str(),
        &environment.fog.startDistance, 0.1f, 0.0f, 500.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.endDistance").c_str(),
        &environment.fog.endDistance, 0.1f, 0.1f, 1000.0f);
    ImGui::DragFloat(
        TEXT::Localize("ui.environment.heightFalloff").c_str(),
        &environment.fog.heightFalloff, 0.001f, 0.0f, 2.0f);
    ImGui::SliderFloat(
        TEXT::Localize("ui.environment.anisotropy").c_str(),
        &environment.fog.anisotropy, -0.8f, 0.8f);
    ImGui::SliderFloat(
        TEXT::Localize("ui.environment.temporalWeight").c_str(),
        &environment.fog.temporalWeight, 0.0f, 0.98f);
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.useSkyHorizonColor").c_str(),
        &environment.fog.useSkyHorizonColor);
    if (ImGui::Button(
            TEXT::Localize(
                "ui.environment.setFogFromSkyHorizon").c_str())) {
      environment.fog.color = environment.sky.horizonColor;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode(TEXT::Localize("ui.environment.debug").c_str())) {
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.showLightDebug").c_str(),
        &environment.showLightDebug);
    ImGui::Checkbox(
        TEXT::Localize("ui.environment.showSkyDebugInfo").c_str(),
        &environment.showSkyDebugInfo);

    ImGui::TextDisabled(
        TEXT::Localize("ui.environment.runtimeFormat").c_str(),
        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
        runtimeSnapshot.recentRenderErrorCount);
    if (runtimeSnapshot.recentRenderErrorCount > 0) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
          "%s",
          TEXT::Localize(
              "ui.environment.renderDiagnosticsErrors").c_str());
    }

    // Debug dump 驍ｵ・ｺ繝ｻ・ｯ scene dirty
    // 驍ｵ・ｺ繝ｻ・ｫ驍ｵ・ｺ陷会ｽｱ遶企・・ｸ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
    if (ImGui::Button(
            TEXT::Localize(
                "ui.environment.dumpEnvironmentDiagnostics").c_str())) {
      RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshot("EnvironmentPanel",
                                                    &environment);
    }
    ImGui::TreePop();
  }

  if (ImGui::Button(
          TEXT::Localize(
              "ui.environment.resetDefaults").c_str())) {
    environment = SceneEnvironment{};
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
