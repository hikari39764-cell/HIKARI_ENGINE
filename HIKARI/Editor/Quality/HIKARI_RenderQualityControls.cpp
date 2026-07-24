#include "Editor/Quality/HIKARI_QualityPanel.h"

#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_PostProfileParameterWidget.h"
#include "HIKARI_Services.h"
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
bool DrawResolutionPresetCombo(const char *label,
                               RENDER3D::RenderResolutionPreset &preset,
                               bool includeViewport) {
  bool changed = false;
  ImGui::PushID(label);
  EDITOR::PropertyLabel(label);
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::RenderResolutionPresetLabel(preset))) {
    const RENDER3D::RenderResolutionPreset presets[] = {
        RENDER3D::RenderResolutionPreset::Viewport,
        RENDER3D::RenderResolutionPreset::P720,
        RENDER3D::RenderResolutionPreset::P1080,
        RENDER3D::RenderResolutionPreset::P1440,
        RENDER3D::RenderResolutionPreset::P2160,
    };
    for (RENDER3D::RenderResolutionPreset candidate : presets) {
      if (!includeViewport &&
          candidate == RENDER3D::RenderResolutionPreset::Viewport) {
        continue;
      }
      const bool selected = preset == candidate;
      if (ImGui::Selectable(RENDER3D::RenderResolutionPresetLabel(candidate),
                            selected)) {
        preset = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawWindowModeCombo(const char *label,
                         RENDER3D::WindowPresentationMode &mode) {
  bool changed = false;
  ImGui::PushID(label);
  EDITOR::PropertyLabel(label);
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::WindowPresentationModeLabel(mode))) {
    const RENDER3D::WindowPresentationMode modes[] = {
        RENDER3D::WindowPresentationMode::Windowed,
        RENDER3D::WindowPresentationMode::BorderlessWindow,
        RENDER3D::WindowPresentationMode::Fullscreen,
    };
    for (RENDER3D::WindowPresentationMode candidate : modes) {
      const bool selected = mode == candidate;
      if (ImGui::Selectable(RENDER3D::WindowPresentationModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawGeometryPipelineCombo(RENDER3D::GeometryPipelineMode &mode) {
  bool changed = false;
  ImGui::PushID("Geometry Backend");
  EDITOR::PropertyLabel("Geometry Backend");
  if (ImGui::BeginCombo("##Value", RENDER3D::GeometryPipelineModeLabel(mode))) {
    const RENDER3D::GeometryPipelineMode modes[] = {
        RENDER3D::GeometryPipelineMode::MeshShader,
        RENDER3D::GeometryPipelineMode::TraditionalVsPs,
        RENDER3D::GeometryPipelineMode::AutoFallback,
    };
    for (RENDER3D::GeometryPipelineMode candidate : modes) {
      const bool selected = mode == candidate;
      if (ImGui::Selectable(RENDER3D::GeometryPipelineModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawForwardCostModeCombo(RENDER3D::ForwardShadingCostMode &mode) {
  bool changed = false;
  ImGui::PushID("Forward Cost Mode");
  EDITOR::PropertyLabel("Forward Cost Mode");
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::ForwardShadingCostModeLabel(mode))) {
    const RENDER3D::ForwardShadingCostMode modes[] = {
        RENDER3D::ForwardShadingCostMode::Full,
        RENDER3D::ForwardShadingCostMode::AlbedoOnly,
        RENDER3D::ForwardShadingCostMode::NoNormalMap,
        RENDER3D::ForwardShadingCostMode::NoShadow,
        RENDER3D::ForwardShadingCostMode::NoSsao,
        RENDER3D::ForwardShadingCostMode::NoMaterialExtras,
    };
    for (RENDER3D::ForwardShadingCostMode candidate : modes) {
      const bool selected = mode == candidate;
      if (ImGui::Selectable(RENDER3D::ForwardShadingCostModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawLightProbeVolumeSamplingCombo(
    RENDER3D::LightProbeVolumeSamplingMode &mode) {
  bool changed = false;
  ImGui::PushID("Light Probe Sampling");
  EDITOR::PropertyLabel("Light Probe Sampling");
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::LightProbeVolumeSamplingModeLabel(mode))) {
    const RENDER3D::LightProbeVolumeSamplingMode modes[] = {
        RENDER3D::LightProbeVolumeSamplingMode::FastSmooth,
        RENDER3D::LightProbeVolumeSamplingMode::FullTrilinear,
        RENDER3D::LightProbeVolumeSamplingMode::Off,
    };
    for (RENDER3D::LightProbeVolumeSamplingMode candidate : modes) {
      const bool selected = mode == candidate;
      if (ImGui::Selectable(
              RENDER3D::LightProbeVolumeSamplingModeLabel(candidate),
              selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawAntiAliasingModeCombo(RENDER3D::RenderAntiAliasingMode &mode) {
  bool changed = false;
  ImGui::PushID("Anti-Aliasing");
  EDITOR::PropertyLabel("Anti-Aliasing");
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::RenderAntiAliasingModeLabel(mode))) {
    const RENDER3D::RenderAntiAliasingMode modes[] = {
        RENDER3D::RenderAntiAliasingMode::Off,
        RENDER3D::RenderAntiAliasingMode::FXAA,
        RENDER3D::RenderAntiAliasingMode::TAA,
        RENDER3D::RenderAntiAliasingMode::DLAA,
        RENDER3D::RenderAntiAliasingMode::DLSS,
    };
    for (RENDER3D::RenderAntiAliasingMode candidate : modes) {
      const bool selected = mode == candidate;
      const bool available = RENDER3D::IsAntiAliasingModeAvailable(candidate);
      ImGui::BeginDisabled(!available);
      if (ImGui::Selectable(RENDER3D::RenderAntiAliasingModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
      ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawDlssQualityModeCombo(RENDER3D::DlssQualityMode &mode) {
  bool changed = false;
  ImGui::PushID("DLSS Quality");
  EDITOR::PropertyLabel("DLSS Quality");
  if (ImGui::BeginCombo("##Value", RENDER3D::DlssQualityModeLabel(mode))) {
    const RENDER3D::DlssQualityMode modes[] = {
        RENDER3D::DlssQualityMode::Quality,
        RENDER3D::DlssQualityMode::Balanced,
        RENDER3D::DlssQualityMode::Performance,
        RENDER3D::DlssQualityMode::UltraPerformance,
    };
    for (RENDER3D::DlssQualityMode candidate : modes) {
      const bool selected = mode == candidate;
      if (ImGui::Selectable(RENDER3D::DlssQualityModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawFrameGenerationModeCombo(RENDER3D::RenderFrameGenerationMode &mode) {
  bool changed = false;
  ImGui::PushID("Frame Generation");
  EDITOR::PropertyLabel("Frame Generation");
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::RenderFrameGenerationModeLabel(mode))) {
    const RENDER3D::RenderFrameGenerationMode modes[] = {
        RENDER3D::RenderFrameGenerationMode::Off,
        RENDER3D::RenderFrameGenerationMode::Dlss,
    };
    for (RENDER3D::RenderFrameGenerationMode candidate : modes) {
      const bool selected = mode == candidate;
      const bool available =
          RENDER3D::IsFrameGenerationModeAvailable(candidate);
      ImGui::BeginDisabled(!available);
      if (ImGui::Selectable(RENDER3D::RenderFrameGenerationModeLabel(candidate),
                            selected)) {
        mode = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
      ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawVolumetricLightingQualityCombo(
    RENDER3D::VolumetricLightingQuality &quality) {

  bool changed = false;
  ImGui::PushID("Volumetric Lighting");
  EDITOR::PropertyLabel("Volumetric Lighting");
  if (ImGui::BeginCombo("##Value",
                        RENDER3D::VolumetricLightingQualityLabel(quality))) {
    const RENDER3D::VolumetricLightingQuality levels[] = {
        RENDER3D::VolumetricLightingQuality::Low,
        RENDER3D::VolumetricLightingQuality::Balanced,
        RENDER3D::VolumetricLightingQuality::High,
    };
    for (RENDER3D::VolumetricLightingQuality candidate : levels) {
      const bool selected = quality == candidate;
      if (ImGui::Selectable(RENDER3D::VolumetricLightingQualityLabel(candidate),
                            selected)) {
        quality = candidate;
        changed = true;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  return changed;
}

bool DrawFxaaSettings() {
  POST::PostSystem::FxaaSettings fxaa = POST::PostSystem::GetFxaaSettings();
  bool changed = false;
  EDITOR::PropertyLabel("Edge Threshold");
  changed |= ImGui::DragFloat("##FxaaEdgeThreshold", &fxaa.edgeThreshold,
                              0.001f, 0.0312f, 0.333f, "%.4f");
  EDITOR::PropertyLabel("Edge Threshold Min");
  changed |= ImGui::DragFloat("##FxaaEdgeThresholdMin", &fxaa.edgeThresholdMin,
                              0.0005f, 0.0f, 0.0833f, "%.4f");
  EDITOR::PropertyLabel("Subpixel Quality");
  changed |= ImGui::DragFloat("##FxaaSubpixelQuality", &fxaa.subpixelQuality,
                              0.01f, 0.0f, 1.0f);
  if (changed) {
    POST::PostSystem::SetFxaaSettings(fxaa);
  }
  return changed;
}

bool DrawRenderSettings() {
  const RENDER3D::RenderQualitySettings beforeEdit =
      RENDER3D::GetRenderQualitySettings();
  RENDER3D::RenderQualitySettings settings = beforeEdit;
  bool changed = false;

  if (ImGui::TreeNodeEx("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (EDITOR::BeginPropertyTable("QualityDisplayProperties")) {
      changed |= DrawResolutionPresetCombo("Render Resolution",
                                           settings.sceneResolution, true);
      if (settings.sceneResolution ==
          RENDER3D::RenderResolutionPreset::Viewport) {
        EDITOR::PropertyLabel("Viewport Scale");
        changed |= ImGui::SliderFloat(
            "##ViewportScale", &settings.viewportScale, 0.25f, 2.0f, "%.2fx");
      }
      changed |= DrawResolutionPresetCombo("Game Window Size",
                                           settings.windowSize, false);
      changed |= DrawWindowModeCombo("Game Window Mode", settings.windowMode);
      EDITOR::PropertyLabel("VSync");
      changed |= ImGui::Checkbox("##VSync", &settings.vSync);

      int captureWidth = 0;
      int captureHeight = 0;
      int outputWidth = 0;
      int outputHeight = 0;
      POST::PostSystem::GetSceneCaptureSize(captureWidth, captureHeight);
      POST::PostSystem::GetSceneOutputSize(outputWidth, outputHeight);
      EDITOR::PropertyLabel("Internal Render");
      ImGui::TextDisabled("%d x %d", captureWidth, captureHeight);
      EDITOR::PropertyLabel("Temporal Output");
      ImGui::TextDisabled("%d x %d", outputWidth, outputHeight);
      EDITOR::EndPropertyTable();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (EDITOR::BeginPropertyTable("QualityPipelineProperties")) {
      changed |= DrawGeometryPipelineCombo(settings.geometryPipeline);
      changed |= DrawForwardCostModeCombo(settings.forwardCostMode);
      changed |=
          DrawLightProbeVolumeSamplingCombo(settings.lightProbeVolumeSampling);
      changed |= DrawVolumetricLightingQualityCombo(
          settings.volumetricLightingQuality);
      changed |= DrawAntiAliasingModeCombo(settings.antiAliasingMode);
      if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS) {
        changed |= DrawDlssQualityModeCombo(settings.dlssQualityMode);
      }
      changed |= DrawFrameGenerationModeCombo(settings.frameGenerationMode);
      if (settings.frameGenerationMode ==
          RENDER3D::RenderFrameGenerationMode::Dlss) {
        int multiplier = settings.frameGenerationMultiplier;
        EDITOR::PropertyLabel("Frame Multiplier");
        if (ImGui::SliderInt("##FrameGenerationMultiplier", &multiplier, 2, 6,
                             "%dx")) {
          settings.frameGenerationMultiplier = static_cast<uint8_t>(multiplier);
          changed = true;
        }
      }
      if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::TAA) {
        EDITOR::PropertyLabel("TAA History");
        changed |= ImGui::SliderFloat(
            "##TaaHistory", &settings.taaHistoryWeight, 0.0f, 0.97f, "%.2f");
        EDITOR::PropertyLabel("Variance Clip");
        changed |= ImGui::SliderFloat("##TaaVarianceClip",
                                      &settings.taaVarianceClipGamma, 0.0f,
                                      3.0f, "%.2f");
        EDITOR::PropertyLabel("Depth Reject");
        changed |=
            ImGui::DragFloat("##TaaDepthReject", &settings.taaDepthRejection,
                             0.0005f, 0.0001f, 0.05f, "%.4f");
        EDITOR::PropertyLabel("Luma Reject");
        changed |= ImGui::SliderFloat("##TaaLumaReject",
                                      &settings.taaLuminanceRejection, 0.05f,
                                      4.0f, "%.2f");
        EDITOR::PropertyLabel("Sharpness");
        changed |= ImGui::SliderFloat("##TaaSharpness", &settings.taaSharpness,
                                      0.0f, 1.0f, "%.2f");
      } else if (settings.antiAliasingMode ==
                 RENDER3D::RenderAntiAliasingMode::FXAA) {
        (void)DrawFxaaSettings();
      }
      EDITOR::PropertyLabel("Depth Prepass");
      changed |=
          ImGui::Checkbox("##SceneDepthPrepass", &settings.sceneDepthPrepass);
      EDITOR::EndPropertyTable();
    }
    ImGui::TreePop();
  }

  if (changed) {
    RENDER3D::SetRenderQualitySettings(settings);
  }
  return !RENDER3D::AreRenderQualitySettingsEqual(
      beforeEdit, RENDER3D::GetRenderQualitySettings());
}
#endif

} // namespace HIKARI::EDITOR::QUALITY_PANEL
