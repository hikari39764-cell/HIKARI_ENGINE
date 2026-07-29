#include "Editor/Quality/HIKARI_QualityPanel.h"

#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_PostProfileParameterWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#endif
#include "Editor/Quality/HIKARI_QualityPanelSections.h"

namespace HIKARI::EDITOR::QUALITY_PANEL {

#if defined(HIKARI_WITH_EDITOR)
constexpr const char *kDefaultGlobalPostProfileId = "Ani";

void DrawDirectionalShadow(SceneEnvironment &environment) {
  ImGui::Checkbox("Shadow Enabled", &environment.directionalShadow.enabled);
  int shadowQuality =
      environment.directionalShadow.resolution <= 1024
          ? 0
          : (environment.directionalShadow.resolution >= 4096 ? 2 : 1);
  const char *qualityNames[] = {"Low", "Medium", "High"};
  if (ImGui::Combo("Shadow Quality", &shadowQuality, qualityNames,
                   static_cast<int>(std::size(qualityNames)))) {
    environment.directionalShadow.resolution =
        shadowQuality == 0 ? 1024u : (shadowQuality == 2 ? 4096u : 2048u);
  }
  int cascadeCount =
      static_cast<int>(environment.directionalShadow.cascadeCount);
  if (ImGui::SliderInt("Cascades", &cascadeCount, 1, 4)) {
    environment.directionalShadow.cascadeCount =
        static_cast<uint32_t>(std::clamp(cascadeCount, 1, 4));
  }
  ImGui::DragFloat("Shadow Softness", &environment.directionalShadow.pcfRadius,
                   0.05f, 0.0f, 4.0f);
  ImGui::DragFloat("Shadow Distance",
                   &environment.directionalShadow.shadowDistance,
                   0.1f, 1.0f, 200.0f);

  if (ImGui::TreeNode("Advanced Shadow Parameters")) {
    const int resolutions[] = {1024, 2048, 4096};
    int currentResolution =
        static_cast<int>(environment.directionalShadow.resolution);
    if (currentResolution != 1024 && currentResolution != 2048 &&
        currentResolution != 4096) {
      currentResolution = 2048;
    }
    if (ImGui::BeginCombo("Resolution",
                          std::to_string(currentResolution).c_str())) {
      for (int resolution : resolutions) {
        const bool selected = currentResolution == resolution;
        if (ImGui::Selectable(std::to_string(resolution).c_str(), selected)) {
          environment.directionalShadow.resolution =
              static_cast<uint32_t>(resolution);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::DragFloat("Cascade Split Balance",
                     &environment.directionalShadow.cascadeSplitLambda,
                     0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Cascade Blend",
                     &environment.directionalShadow.cascadeBlend,
                     0.005f, 0.0f, 0.3f);
    ImGui::DragFloat("Near Plane", &environment.directionalShadow.nearPlane,
                     0.01f, 0.001f, 50.0f);
    ImGui::DragFloat("Far Plane", &environment.directionalShadow.farPlane, 0.1f,
                     1.0f, 500.0f);
    ImGui::DragFloat("Depth Bias", &environment.directionalShadow.depthBias,
                     0.0001f, 0.0f, 0.05f, "%.5f");
    ImGui::DragFloat("Normal Bias", &environment.directionalShadow.normalBias,
                     0.001f, 0.0f, 1.0f, "%.4f");
    ImGui::DragFloat("Strength", &environment.directionalShadow.strength, 0.01f,
                     0.0f, 1.0f);
    ImGui::Checkbox("PCF Enabled", &environment.directionalShadow.pcfEnabled);
    ImGui::DragFloat("PCF Radius", &environment.directionalShadow.pcfRadius,
                     0.05f, 0.0f, 4.0f);
    ImGui::Checkbox("Stabilize", &environment.directionalShadow.stabilize);
    if (ImGui::TreeNode("Debug Visualization")) {
      ImGui::Checkbox("Show Debug Texture",
                      &environment.directionalShadow.showDebugTexture);
      ImGui::Checkbox("Show Cascade Frusta",
                      &environment.directionalShadow.showDebugFrustum);
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }
}

void DrawScreenSpaceReflections(SceneEnvironment &environment) {
  ScreenSpaceReflectionSettings &settings =
      environment.screenSpaceReflections;
  ImGui::Checkbox("SSR Enabled", &settings.enabled);
  int quality = static_cast<int>(settings.quality);
  const char *qualityNames[] = {"Off", "Low", "Medium", "High"};
  if (ImGui::Combo("Quality", &quality, qualityNames,
                   static_cast<int>(std::size(qualityNames)))) {
    settings.quality = static_cast<ScreenSpaceReflectionQuality>(
        std::clamp(quality, 0, 3));
    settings.enabled =
        settings.quality != ScreenSpaceReflectionQuality::Off;
  }
  ImGui::DragFloat("Intensity", &settings.intensity, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Trace Distance", &settings.maxDistance, 0.25f, 1.0f,
                   200.0f);
  ImGui::DragFloat("Maximum Roughness", &settings.maxRoughness, 0.01f, 0.05f,
                   1.0f);
  ImGui::TextDisabled("Fallback: SSR -> Local Reflection Probe -> Sky IBL");
  ImGui::TextDisabled(
      "SSR only resolves screen-visible hits; off-screen rays keep fallback.");

  if (ImGui::TreeNode("Advanced Ray Marching")) {
    ImGui::DragFloat("Relative Depth Thickness", &settings.thickness, 0.0001f,
                     0.0001f, 0.05f, "%.5f");
    ImGui::DragFloat("Ray Stride", &settings.stride, 0.01f, 0.02f, 2.0f);
    ImGui::DragFloat("Screen Edge Fade", &settings.edgeFade, 0.005f, 0.01f,
                     0.35f);
    ImGui::TreePop();
  }
}

void DrawAmbientOcclusion(
    SceneEnvironment &environment,
    const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot
        &runtimeSnapshot) {
  int ssaoModeIndex =
      EDITOR::ENVIRONMENT_EDIT::ToSsaoModeIndex(environment.ambientOcclusion);
  const char *ssaoModeNames[] = {"Off", "Reference", "OptimizedHigh",
                                 "Balanced"};
  if (ImGui::Combo("SSAO Mode", &ssaoModeIndex, ssaoModeNames,
                   static_cast<int>(std::size(ssaoModeNames)))) {
    environment.ambientOcclusion.mode =
        EDITOR::ENVIRONMENT_EDIT::SsaoModeFromIndex(ssaoModeIndex);
    environment.ambientOcclusion.enabled =
        environment.ambientOcclusion.mode != SsaoMode::Off;
  }
  ImGui::DragFloat("Radius", &environment.ambientOcclusion.radius, 0.01f, 0.01f,
                   10.0f);
  ImGui::DragFloat("Bias", &environment.ambientOcclusion.bias, 0.001f, 0.0f,
                   0.5f, "%.4f");
  ImGui::DragFloat("Strength", &environment.ambientOcclusion.strength, 0.01f,
                   0.0f, 4.0f);
  ImGui::DragFloat("Power", &environment.ambientOcclusion.power, 0.01f, 0.1f,
                   8.0f);
  ImGui::DragFloat("Diffuse Strength",
                   &environment.ambientOcclusion.diffuseStrength, 0.01f, 0.0f,
                   1.0f);
  ImGui::DragFloat("Specular Strength",
                   &environment.ambientOcclusion.specularStrength, 0.01f, 0.0f,
                   1.0f);

  int sampleIndex = 0;
  const uint32_t samples = environment.ambientOcclusion.sampleCount;
  if (samples <= 8u)
    sampleIndex = 0;
  else if (samples <= 16u)
    sampleIndex = 1;
  else if (samples <= 24u)
    sampleIndex = 2;
  else
    sampleIndex = 3;
  const char *sampleNames[] = {"8", "16", "24", "32"};
  if (ImGui::Combo("Samples", &sampleIndex, sampleNames,
                   static_cast<int>(std::size(sampleNames)))) {
    const uint32_t sampleValues[] = {8u, 16u, 24u, 32u};
    environment.ambientOcclusion.sampleCount =
        sampleValues[std::clamp(sampleIndex, 0, 3)];
  }
  int blurIterations =
      static_cast<int>(environment.ambientOcclusion.blurIterations);
  if (ImGui::SliderInt("Blur Iterations", &blurIterations, 0, 4)) {
    environment.ambientOcclusion.blurIterations =
        static_cast<uint32_t>(std::clamp(blurIterations, 0, 4));
  }
  ImGui::TextDisabled(
      "Runtime AO: %s",
      RENDER3D::DIAGNOSTICS::ResolveSsaoSummaryLabel(runtimeSnapshot));
}

void DrawBloom(SceneEnvironment &environment) {
  ImGui::Checkbox("Bloom Enabled", &environment.bloom.enabled);
  ImGui::DragFloat("Threshold", &environment.bloom.threshold, 0.01f, 0.0f,
                   10.0f);
  ImGui::DragFloat("Intensity", &environment.bloom.intensity, 0.01f, 0.0f,
                   5.0f);
  ImGui::DragFloat("Radius", &environment.bloom.radius, 0.01f, 0.0f, 8.0f);
  int downsampleCount = static_cast<int>(environment.bloom.downsampleCount);
  if (ImGui::SliderInt("Downsample Count", &downsampleCount, 1, 5)) {
    environment.bloom.downsampleCount =
        static_cast<uint32_t>(std::clamp(downsampleCount, 1, 5));
  }
}

void DrawToneMapping(SceneEnvironment &environment) {
  ImGui::Checkbox("Tone Curve Enabled", &environment.toneMapping.enabled);
  ImGui::TextDisabled(
      "Exposure and SDR display transfer remain active when the curve is off.");
  int exposureMode =
      environment.toneMapping.exposureMode ==
              RENDER3D::COLOR::ExposureMode::Automatic
          ? 1
          : 0;
  const char *exposureModes[] = {"Manual", "Automatic"};
  if (ImGui::Combo("Exposure Mode", &exposureMode, exposureModes,
                   static_cast<int>(std::size(exposureModes)))) {
    environment.toneMapping.exposureMode =
        exposureMode == 1 ? RENDER3D::COLOR::ExposureMode::Automatic
                          : RENDER3D::COLOR::ExposureMode::Manual;
  }
  const char *exposureLabel =
      environment.toneMapping.exposureMode ==
              RENDER3D::COLOR::ExposureMode::Automatic
          ? "Exposure Seed"
          : "Manual Exposure";
  ImGui::DragFloat(exposureLabel, &environment.toneMapping.exposure, 0.01f,
                   0.01f, 16.0f);
  if (environment.toneMapping.exposureMode ==
      RENDER3D::COLOR::ExposureMode::Automatic) {
    auto &automatic = environment.toneMapping.automaticExposure;
    ImGui::Indent();
    ImGui::DragFloat("Target Grey", &automatic.targetLuminance, 0.001f, 0.02f,
                     0.5f, "%.3f");
    ImGui::DragFloat("Exposure Compensation (EV)", &automatic.compensationEv,
                     0.05f, -8.0f, 8.0f, "%+.2f");
    ImGui::DragFloat("Minimum Exposure (EV)", &automatic.minExposureEv, 0.05f,
                     -16.0f, automatic.maxExposureEv);
    ImGui::DragFloat("Maximum Exposure (EV)", &automatic.maxExposureEv, 0.05f,
                     automatic.minExposureEv, 16.0f);
    ImGui::DragFloat("Bright Adapt Speed", &automatic.speedUp, 0.05f, 0.0f,
                     20.0f);
    ImGui::DragFloat("Dark Adapt Speed", &automatic.speedDown, 0.05f, 0.0f,
                     20.0f);
    const auto &runtimeExposure =
        POST::PostSystem::GetAutomaticExposureDebugState();
    if (runtimeExposure.measurementValid) {
      ImGui::TextDisabled("Meter %.3f | Exposure %.3f -> %.3f",
                          runtimeExposure.meteredLuminance,
                          runtimeExposure.currentExposure,
                          runtimeExposure.targetExposure);
    } else {
      ImGui::TextDisabled("Waiting for the first HDR luminance sample...");
    }
    ImGui::Unindent();
  }
  ImGui::SeparatorText("Display Color");
  ImGui::DragFloat("White Balance (K)",
                   &environment.toneMapping.whiteBalanceTemperature, 25.0f,
                   2000.0f, 15000.0f, "%.0f K");
  ImGui::DragFloat("White Balance Tint",
                   &environment.toneMapping.whiteBalanceTint, 0.01f, -1.0f,
                   1.0f);
  ImGui::DragFloat("Contrast", &environment.toneMapping.contrast, 0.01f, 0.25f,
                   2.0f);
  ImGui::DragFloat("Saturation", &environment.toneMapping.saturation, 0.01f,
                   0.0f, 2.0f);
  ImGui::DragFloat("SDR Display Gamma", &environment.toneMapping.gamma, 0.01f,
                   0.1f, 4.0f);
  const char *modes[] = {"None", "Reinhard", "ACES Fitted"};
  int mode = std::clamp(environment.toneMapping.mode, 0, 2);
  if (ImGui::Combo("Mode", &mode, modes, static_cast<int>(std::size(modes)))) {
    environment.toneMapping.mode = mode;
  }
}

void DrawGlobalPost(SceneEnvironment &environment) {
  const bool wasPostEnabled = environment.post.enabled;
  if (ImGui::Checkbox("Post Enabled", &environment.post.enabled) &&
      environment.post.enabled && !wasPostEnabled &&
      environment.post.globalPostProfileId.empty()) {
    environment.post.globalPostProfileId = kDefaultGlobalPostProfileId;
    environment.post.valuesInitialized = false;
  }

  const std::string previousProfileId = environment.post.globalPostProfileId;
  char profileBuffer[256]{};
  std::strncpy(profileBuffer, environment.post.globalPostProfileId.c_str(),
               sizeof(profileBuffer) - 1);
  if (ImGui::InputText("Global Post Profile", profileBuffer,
                       sizeof(profileBuffer))) {
    environment.post.globalPostProfileId = profileBuffer;
    if (environment.post.globalPostProfileId.empty()) {
      environment.post.valuesInitialized = false;
    }
  }

  if (!environment.post.globalPostProfileId.empty()) {
    PostProfile profile{};
    if (PostProfile::LoadById(environment.post.globalPostProfileId, profile)) {
      const bool profileChanged =
          environment.post.globalPostProfileId != previousProfileId;
      if (profileChanged || !environment.post.valuesInitialized) {
        profile.CopyValuesTo(environment.post.paramValues);
        environment.post.valuesInitialized = true;
      }

      if (ImGui::Button("Reset To Profile Defaults")) {
        profile.CopyValuesTo(environment.post.paramValues);
        environment.post.valuesInitialized = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Reload Profile")) {
        PostProfile reloadedProfile{};
        if (PostProfile::LoadById(environment.post.globalPostProfileId,
                                  reloadedProfile)) {
          profile = std::move(reloadedProfile);
        }
      }

      if (ImGui::TreeNode("Profile Parameters")) {
        for (const VFX::ParamDesc &param : profile.params) {
          const int slot = static_cast<int>(param.ref.slot);
          if (slot < 0 || slot >= 16 || param.ref.channel >= 4) {
            continue;
          }
          ImGui::PushID(param.key.c_str());
          EDITOR::DrawPostProfileParameter(param,
                                           environment.post.paramValues[slot]);
          ImGui::PopID();
        }
        ImGui::TreePop();
      }
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                         "Profile not found: %s",
                         environment.post.globalPostProfileId.c_str());
    }
  }

  if (ImGui::TreeNode("Advanced Raw Parameter Block")) {
    for (int i = 0; i < 16; ++i) {
      ImGui::PushID(i);
      ImGui::InputFloat4("Param", &environment.post.paramValues[i].x);
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
}

void DrawTransitionDebug() {
  const SceneTransitionBus *transitionBus =
      RuntimeSceneContext::GetTransitionBus();
  if (!transitionBus) {
    ImGui::TextDisabled("Transition bus unavailable.");
    return;
  }

  const TransitionVisualState visualState = transitionBus->GetVisualState();
  const SceneTransitionBus::TransitionState state = transitionBus->GetState();
  const char *stateLabel = "Unknown";
  switch (state) {
  case SceneTransitionBus::TransitionState::Idle:
    stateLabel = "Idle";
    break;
  case SceneTransitionBus::TransitionState::TransitionOut:
    stateLabel = "TransitionOut";
    break;
  case SceneTransitionBus::TransitionState::SwitchingScene:
    stateLabel = "SwitchingScene";
    break;
  case SceneTransitionBus::TransitionState::TransitionIn:
    stateLabel = "TransitionIn";
    break;
  default:
    break;
  }

  ImGui::Text("Profile Id: %s", visualState.profileId.empty()
                                    ? "<none>"
                                    : visualState.profileId.c_str());
  ImGui::Text("State: %s", stateLabel);
  ImGui::Text("Progress: %.3f", visualState.progress);
  ImGui::Text("Out Duration: %.3f", visualState.outDuration);
  ImGui::Text("In Duration: %.3f", visualState.inDuration);
}
#endif

} // namespace HIKARI::EDITOR::QUALITY_PANEL
