#include "Editor/Quality/HIKARI_QualityPanelSections.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#endif

namespace HIKARI::EDITOR::QUALITY_PANEL {

#if defined(HIKARI_WITH_EDITOR)
bool DrawResolutionPresetCombo(const char* label, RENDER3D::RenderResolutionPreset& preset, bool includeViewport) {
    bool changed = false;
    ImGui::PushID(label);
    EDITOR::PropertyLabel(label);
    if (ImGui::BeginCombo("##Value", RENDER3D::RenderResolutionPresetLabel(preset))) {
        const RENDER3D::RenderResolutionPreset presets[] = {
            RENDER3D::RenderResolutionPreset::Viewport, RENDER3D::RenderResolutionPreset::P720,
            RENDER3D::RenderResolutionPreset::P1080,    RENDER3D::RenderResolutionPreset::P1440,
            RENDER3D::RenderResolutionPreset::P2160,
        };
        for (RENDER3D::RenderResolutionPreset candidate : presets) {
            if (!includeViewport && candidate == RENDER3D::RenderResolutionPreset::Viewport) {
                continue;
            }
            const bool selected = preset == candidate;
            if (ImGui::Selectable(RENDER3D::RenderResolutionPresetLabel(candidate), selected)) {
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

bool DrawWindowModeCombo(const char* label, RENDER3D::WindowPresentationMode& mode) {
    bool changed = false;
    ImGui::PushID(label);
    EDITOR::PropertyLabel(label);
    if (ImGui::BeginCombo("##Value", RENDER3D::WindowPresentationModeLabel(mode))) {
        const RENDER3D::WindowPresentationMode modes[] = {
            RENDER3D::WindowPresentationMode::Windowed,
            RENDER3D::WindowPresentationMode::BorderlessWindow,
            RENDER3D::WindowPresentationMode::Fullscreen,
        };
        for (RENDER3D::WindowPresentationMode candidate : modes) {
            const bool selected = mode == candidate;
            if (ImGui::Selectable(RENDER3D::WindowPresentationModeLabel(candidate), selected)) {
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

bool DrawGeometryPipelineCombo(RENDER3D::GeometryPipelineMode& mode) {
    bool changed = false;
    ImGui::PushID("Geometry Backend");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.geometryBackend").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::GeometryPipelineModeLabel(mode))) {
        const RENDER3D::GeometryPipelineMode modes[] = {
            RENDER3D::GeometryPipelineMode::MeshShader,
            RENDER3D::GeometryPipelineMode::TraditionalVsPs,
            RENDER3D::GeometryPipelineMode::AutoFallback,
        };
        for (RENDER3D::GeometryPipelineMode candidate : modes) {
            const bool selected = mode == candidate;
            if (ImGui::Selectable(RENDER3D::GeometryPipelineModeLabel(candidate), selected)) {
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

bool DrawForwardCostModeCombo(RENDER3D::ForwardShadingCostMode& mode) {
    bool changed = false;
    ImGui::PushID("Forward Cost Mode");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.forwardCostMode").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::ForwardShadingCostModeLabel(mode))) {
        const RENDER3D::ForwardShadingCostMode modes[] = {
            RENDER3D::ForwardShadingCostMode::Full,        RENDER3D::ForwardShadingCostMode::AlbedoOnly,
            RENDER3D::ForwardShadingCostMode::NoNormalMap, RENDER3D::ForwardShadingCostMode::NoShadow,
            RENDER3D::ForwardShadingCostMode::NoSsao,      RENDER3D::ForwardShadingCostMode::NoMaterialExtras,
        };
        for (RENDER3D::ForwardShadingCostMode candidate : modes) {
            const bool selected = mode == candidate;
            if (ImGui::Selectable(RENDER3D::ForwardShadingCostModeLabel(candidate), selected)) {
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

bool DrawLightProbeVolumeSamplingCombo(RENDER3D::LightProbeVolumeSamplingMode& mode) {
    bool changed = false;
    ImGui::PushID("Light Probe Sampling");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.lightProbeSampling").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::LightProbeVolumeSamplingModeLabel(mode))) {
        const RENDER3D::LightProbeVolumeSamplingMode modes[] = {
            RENDER3D::LightProbeVolumeSamplingMode::FastSmooth,
            RENDER3D::LightProbeVolumeSamplingMode::FullTrilinear,
            RENDER3D::LightProbeVolumeSamplingMode::Off,
        };
        for (RENDER3D::LightProbeVolumeSamplingMode candidate : modes) {
            const bool selected = mode == candidate;
            if (ImGui::Selectable(RENDER3D::LightProbeVolumeSamplingModeLabel(candidate), selected)) {
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

bool DrawAntiAliasingModeCombo(RENDER3D::RenderAntiAliasingMode& mode) {
    bool changed = false;
    ImGui::PushID("Anti-Aliasing");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.antiAliasing").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::RenderAntiAliasingModeLabel(mode))) {
        const RENDER3D::RenderAntiAliasingMode modes[] = {
            RENDER3D::RenderAntiAliasingMode::Off,  RENDER3D::RenderAntiAliasingMode::FXAA,
            RENDER3D::RenderAntiAliasingMode::TAA,  RENDER3D::RenderAntiAliasingMode::DLAA,
            RENDER3D::RenderAntiAliasingMode::DLSS,
        };
        for (RENDER3D::RenderAntiAliasingMode candidate : modes) {
            const bool selected = mode == candidate;
            const bool available = RENDER3D::IsAntiAliasingModeAvailable(candidate);
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(RENDER3D::RenderAntiAliasingModeLabel(candidate), selected)) {
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

bool DrawDlssQualityModeCombo(RENDER3D::DlssQualityMode& mode) {
    bool changed = false;
    ImGui::PushID("DLSS Quality");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.dlssQuality").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::DlssQualityModeLabel(mode))) {
        const RENDER3D::DlssQualityMode modes[] = {
            RENDER3D::DlssQualityMode::Quality,
            RENDER3D::DlssQualityMode::Balanced,
            RENDER3D::DlssQualityMode::Performance,
            RENDER3D::DlssQualityMode::UltraPerformance,
        };
        for (RENDER3D::DlssQualityMode candidate : modes) {
            const bool selected = mode == candidate;
            if (ImGui::Selectable(RENDER3D::DlssQualityModeLabel(candidate), selected)) {
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

bool DrawFrameGenerationModeCombo(RENDER3D::RenderFrameGenerationMode& mode) {
    bool changed = false;
    ImGui::PushID("Frame Generation");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.frameGeneration").c_str());
    if (ImGui::BeginCombo("##Value", RENDER3D::RenderFrameGenerationModeLabel(mode))) {
        const RENDER3D::RenderFrameGenerationMode modes[] = {
            RENDER3D::RenderFrameGenerationMode::Off,
            RENDER3D::RenderFrameGenerationMode::Dlss,
        };
        for (RENDER3D::RenderFrameGenerationMode candidate : modes) {
            const bool selected = mode == candidate;
            const bool available = RENDER3D::IsFrameGenerationModeAvailable(candidate);
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(RENDER3D::RenderFrameGenerationModeLabel(candidate), selected)) {
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

template <typename Quality, typename Options, typename GetValue, typename GetName, typename GetCurrentName>
bool DrawQualityCombo(const char* label, Quality& quality, const Options& options, GetValue getValue, GetName getName,
                      GetCurrentName getCurrentName) {
    bool changed = false;
    ImGui::PushID(label);
    EDITOR::PropertyLabel(label);
    if (ImGui::BeginCombo("##Value", getCurrentName(quality))) {
        for (const auto& option : options) {
            const Quality candidate = getValue(option);
            const bool selected = quality == candidate;
            if (ImGui::Selectable(getName(option), selected)) {
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

template <typename Quality>
bool DrawLightingQualityCombo(const char* label, Quality& quality, std::initializer_list<Quality> levels,
                              const char* (*name)(Quality)) {
    return DrawQualityCombo(
        label, quality, levels, [](Quality candidate) { return candidate; },
        [name](Quality candidate) { return name(candidate); }, name);
}

bool DrawDepthOfFieldQualityCombo(RENDER3D::DepthOfFieldQuality& quality) {
    const std::string label = TEXT::Localize("ui.quality.depthOfField");
    return DrawQualityCombo(
        label.c_str(), quality, RENDER3D::kDepthOfFieldQualityNames, [](const auto& option) { return option.value; },
        [](const auto& option) { return option.name; }, RENDER3D::DepthOfFieldQualityLabel);
}

bool DrawFxaaSettings() {
    POST::PostSystem::FxaaSettings fxaa = POST::PostSystem::GetFxaaSettings();
    bool changed = false;
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.edgeThreshold").c_str());
    changed |= ImGui::DragFloat("##FxaaEdgeThreshold", &fxaa.edgeThreshold, 0.001f, 0.0312f, 0.333f, "%.4f");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.edgeThresholdMin").c_str());
    changed |= ImGui::DragFloat("##FxaaEdgeThresholdMin", &fxaa.edgeThresholdMin, 0.0005f, 0.0f, 0.0833f, "%.4f");
    EDITOR::PropertyLabel(TEXT::Localize("ui.quality.subpixelQuality").c_str());
    changed |= ImGui::DragFloat("##FxaaSubpixelQuality", &fxaa.subpixelQuality, 0.01f, 0.0f, 1.0f);
    if (changed) {
        POST::PostSystem::SetFxaaSettings(fxaa);
    }
    return changed;
}

bool DrawRenderSettings() {
    const RENDER3D::RenderQualitySettings beforeEdit = RENDER3D::GetRenderQualitySettings();
    RENDER3D::RenderQualitySettings settings = beforeEdit;
    bool changed = false;

    if (ImGui::TreeNodeEx(TEXT::Localize("ui.quality.display").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EDITOR::BeginPropertyTable("QualityDisplayProperties")) {
            changed |= DrawResolutionPresetCombo(TEXT::Localize("ui.quality.renderResolution").c_str(),
                                                 settings.sceneResolution, true);
            if (settings.sceneResolution == RENDER3D::RenderResolutionPreset::Viewport) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.viewportScale").c_str());
                changed |= ImGui::SliderFloat("##ViewportScale", &settings.viewportScale, 0.25f, 2.0f, "%.2fx");
            }
            changed |= DrawResolutionPresetCombo(TEXT::Localize("ui.quality.gameWindowSize").c_str(),
                                                 settings.windowSize, false);
            changed |= DrawWindowModeCombo(TEXT::Localize("ui.quality.gameWindowMode").c_str(), settings.windowMode);
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.vsync").c_str());
            changed |= ImGui::Checkbox("##VSync", &settings.vSync);

            int captureWidth = 0;
            int captureHeight = 0;
            int outputWidth = 0;
            int outputHeight = 0;
            POST::PostSystem::GetSceneCaptureSize(captureWidth, captureHeight);
            POST::PostSystem::GetSceneOutputSize(outputWidth, outputHeight);
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.internalRender").c_str());
            ImGui::TextDisabled("%d x %d", captureWidth, captureHeight);
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.temporalOutput").c_str());
            ImGui::TextDisabled("%d x %d", outputWidth, outputHeight);
            EDITOR::EndPropertyTable();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(TEXT::Localize("ui.quality.pipeline").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EDITOR::BeginPropertyTable("QualityPipelineProperties")) {
            changed |= DrawGeometryPipelineCombo(settings.geometryPipeline);
            changed |= DrawForwardCostModeCombo(settings.forwardCostMode);
            changed |= DrawLightProbeVolumeSamplingCombo(settings.lightProbeVolumeSampling);
            changed |= DrawLightingQualityCombo(
                TEXT::Localize("ui.quality.volumetricLighting").c_str(), settings.volumetricLightingQuality,
                {RENDER3D::VolumetricLightingQuality::Low, RENDER3D::VolumetricLightingQuality::Balanced,
                 RENDER3D::VolumetricLightingQuality::High},
                RENDER3D::VolumetricLightingQualityLabel);
            changed |= DrawDepthOfFieldQualityCombo(settings.depthOfFieldQuality);
            changed |= DrawLightingQualityCombo(
                TEXT::Localize("ui.quality.volumetricClouds").c_str(), settings.volumetricCloudQuality,
                {RENDER3D::VolumetricCloudQuality::Low, RENDER3D::VolumetricCloudQuality::Balanced,
                 RENDER3D::VolumetricCloudQuality::High},
                RENDER3D::VolumetricCloudQualityLabel);
            changed |=
                DrawLightingQualityCombo(TEXT::Localize("ui.quality.dynamicGi").c_str(), settings.dynamicGiQuality,
                                         {RENDER3D::DynamicGiQuality::Off, RENDER3D::DynamicGiQuality::Low,
                                          RENDER3D::DynamicGiQuality::Balanced, RENDER3D::DynamicGiQuality::High},
                                         RENDER3D::DynamicGiQualityLabel);
            changed |=
                DrawLightingQualityCombo(TEXT::Localize("ui.quality.localShadows").c_str(), settings.localShadowQuality,
                                         {RENDER3D::LocalShadowQuality::Off, RENDER3D::LocalShadowQuality::Low,
                                          RENDER3D::LocalShadowQuality::Balanced, RENDER3D::LocalShadowQuality::High},
                                         RENDER3D::LocalShadowQualityLabel);
            changed |= DrawAntiAliasingModeCombo(settings.antiAliasingMode);
            if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS) {
                changed |= DrawDlssQualityModeCombo(settings.dlssQualityMode);
            }
            changed |= DrawFrameGenerationModeCombo(settings.frameGenerationMode);
            if (settings.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss &&
                !RENDER3D::UPSCALING::IsStreamlineFrameGenerationInitialized()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.frameGenerationRestart").c_str());
            }
            if (settings.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss) {
                int multiplier = settings.frameGenerationMultiplier;
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.frameMultiplier").c_str());
                if (ImGui::SliderInt("##FrameGenerationMultiplier", &multiplier, 2, 6, "%dx")) {
                    settings.frameGenerationMultiplier = static_cast<uint8_t>(multiplier);
                    changed = true;
                }
            }
            if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::TAA) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.taaHistory").c_str());
                changed |= ImGui::SliderFloat("##TaaHistory", &settings.taaHistoryWeight, 0.0f, 0.97f, "%.2f");
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.varianceClip").c_str());
                changed |= ImGui::SliderFloat("##TaaVarianceClip", &settings.taaVarianceClipGamma, 0.0f, 3.0f, "%.2f");
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.depthReject").c_str());
                changed |=
                    ImGui::DragFloat("##TaaDepthReject", &settings.taaDepthRejection, 0.0005f, 0.0001f, 0.05f, "%.4f");
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.lumaReject").c_str());
                changed |= ImGui::SliderFloat("##TaaLumaReject", &settings.taaLuminanceRejection, 0.05f, 4.0f, "%.2f");
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.sharpness").c_str());
                changed |= ImGui::SliderFloat("##TaaSharpness", &settings.taaSharpness, 0.0f, 1.0f, "%.2f");
            } else if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::FXAA) {
                (void)DrawFxaaSettings();
            }
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.depthPrepass").c_str());
            changed |= ImGui::Checkbox("##SceneDepthPrepass", &settings.sceneDepthPrepass);
            EDITOR::EndPropertyTable();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.quality.directionalShadow").c_str())) {
        changed |= EDITOR::DrawJsonFields("ui.quality.directionalShadow", settings.directionalShadow,
                                          RENDER3D::kDirectionalShadowFields)
                       .changed;
        ImGui::TreePop();
    }

    if (changed) {
        RENDER3D::SetRenderQualitySettings(settings);
    }
    return !RENDER3D::AreRenderQualitySettingsEqual(beforeEdit, RENDER3D::GetRenderQualitySettings());
}
#endif

} // namespace HIKARI::EDITOR::QUALITY_PANEL
