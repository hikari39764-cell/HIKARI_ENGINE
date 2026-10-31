#include "Editor/Quality/HIKARI_QualityPanelSections.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include "imgui.h"
#include <string>
#include <type_traits>

#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "Editor/Widgets/HIKARI_NumberField.h"

namespace HIKARI::EDITOR::QUALITY_PANEL {

// 表の順に選択肢を並べる（表示は labels の名前）。include が偽の候補は出さず、enabled が偽の候補は選べない。
template <class E, size_t N>
bool DrawEnumCombo(const char* label, E& value, const TEXT::EnumName<E> (&labels)[N],
                   std::type_identity_t<bool (*)(E)> include = nullptr,
                   std::type_identity_t<bool (*)(E)> enabled = nullptr) {
    bool changed = false;
    ImGui::PushID(label);
    EDITOR::PropertyLabel(label);
    if (ImGui::BeginCombo("##Value", TEXT::EnumToName(labels, value, "Unknown"))) {
        for (const TEXT::EnumName<E>& option : labels) {
            if (include != nullptr && !include(option.value)) {
                continue;
            }
            const bool selected = value == option.value;
            ImGui::BeginDisabled(enabled != nullptr && !enabled(option.value));
            if (ImGui::Selectable(option.name, selected)) {
                value = option.value;
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
    // 滑块の右クリックで戻す既定値。
    static const RENDER3D::RenderQualitySettings kDefaults{};
    bool changed = false;

    if (ImGui::TreeNodeEx(TEXT::Localize("ui.quality.display").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EDITOR::BeginPropertyTable("QualityDisplayProperties")) {
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.renderResolution").c_str(), settings.sceneResolution,
                                     RENDER3D::kRenderResolutionPresetLabels);
            if (settings.sceneResolution == RENDER3D::RenderResolutionPreset::Viewport) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.viewportScale").c_str());
                changed |= EDITOR::SliderNumber("##ViewportScale", settings.viewportScale, 0.25, 2.0,
                                                &kDefaults.viewportScale);
            }
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.gameWindowSize").c_str(), settings.windowSize,
                                     RENDER3D::kRenderResolutionPresetLabels, RENDER3D::IsFixedRenderResolutionPreset);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.gameWindowMode").c_str(), settings.windowMode,
                                     RENDER3D::kWindowPresentationModeNames);
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
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.geometryBackend").c_str(), settings.geometryPipeline,
                                     RENDER3D::kGeometryPipelineModeLabels);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.forwardCostMode").c_str(), settings.forwardCostMode,
                                     RENDER3D::kForwardShadingCostModeLabels);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.lightProbeSampling").c_str(),
                                     settings.lightProbeVolumeSampling, RENDER3D::kLightProbeVolumeSamplingModeLabels);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.volumetricLighting").c_str(),
                                     settings.volumetricLightingQuality, RENDER3D::kVolumetricLightingQualityNames);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.depthOfField").c_str(), settings.depthOfFieldQuality,
                                     RENDER3D::kDepthOfFieldQualityNames);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.motionBlur").c_str(), settings.motionBlurQuality,
                                     RENDER3D::kMotionBlurQualityNames);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.volumetricClouds").c_str(),
                                     settings.volumetricCloudQuality, RENDER3D::kVolumetricCloudQualityNames);
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.localShadows").c_str(), settings.localShadowQuality,
                                     RENDER3D::kLocalShadowQualityNames);
            changed |=
                DrawEnumCombo(TEXT::Localize("ui.quality.antiAliasing").c_str(), settings.antiAliasingMode,
                              RENDER3D::kRenderAntiAliasingModeNames, nullptr, RENDER3D::IsAntiAliasingModeAvailable);
            if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS) {
                changed |= DrawEnumCombo(TEXT::Localize("ui.quality.dlssQuality").c_str(), settings.dlssQualityMode,
                                         RENDER3D::kDlssQualityModeLabels);
            }
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.frameGeneration").c_str(), settings.frameGenerationMode,
                                     RENDER3D::kRenderFrameGenerationModeLabels, nullptr,
                                     RENDER3D::IsFrameGenerationModeAvailable);
            if (settings.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss &&
                !RENDER3D::UPSCALING::IsStreamlineFrameGenerationInitialized()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.frameGenerationRestart").c_str());
            }
            if (settings.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.frameMultiplier").c_str());
                changed |= EDITOR::SliderNumber("##FrameGenerationMultiplier", settings.frameGenerationMultiplier, 2.0,
                                                6.0, &kDefaults.frameGenerationMultiplier);
            }
            if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::TAA) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.taaHistory").c_str());
                changed |= EDITOR::SliderNumber("##TaaHistory", settings.taaHistoryWeight, 0.0, 0.97,
                                                &kDefaults.taaHistoryWeight);
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.varianceClip").c_str());
                changed |= EDITOR::SliderNumber("##TaaVarianceClip", settings.taaVarianceClipGamma, 0.0, 4.0,
                                                &kDefaults.taaVarianceClipGamma);
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.depthReject").c_str());
                changed |= EDITOR::DragNumber("##TaaDepthReject", settings.taaDepthRejection,
                                              {.minimum = 0.0001, .maximum = 0.05}, &kDefaults.taaDepthRejection);
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.lumaReject").c_str());
                changed |= EDITOR::DragNumber("##TaaLumaReject", settings.taaLuminanceRejection,
                                              {.minimum = 0.05, .maximum = 4.0}, &kDefaults.taaLuminanceRejection);
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.sharpness").c_str());
                changed |=
                    EDITOR::SliderNumber("##TaaSharpness", settings.taaSharpness, 0.0, 1.0, &kDefaults.taaSharpness);
            } else if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::FXAA) {
                (void)DrawFxaaSettings();
            }
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.depthPrepass").c_str());
            changed |= ImGui::Checkbox("##SceneDepthPrepass", &settings.sceneDepthPrepass);
            EDITOR::EndPropertyTable();
        }
        ImGui::TreePop();
    }

    // 光線追跡を使う機能を一か所に並べる（GI・反射・太陽の影）。どれも同じ加速構造を使う。
    if (ImGui::TreeNodeEx(TEXT::Localize("ui.quality.rayTracing").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EDITOR::BeginPropertyTable("QualityRayTracingProperties")) {
            changed |= DrawEnumCombo(TEXT::Localize("ui.quality.dynamicGi").c_str(), settings.dynamicGiQuality,
                                     RENDER3D::kDynamicGiQualityNames);
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.rayTracedReflections").c_str());
            changed |= ImGui::Checkbox("##RayTracedReflections", &settings.rayTracedReflections);
            EDITOR::PropertyLabel(TEXT::Localize("ui.quality.directionalShadow.rayTraced").c_str());
            changed |= ImGui::Checkbox("##RayTracedShadows", &settings.directionalShadow.rayTraced);
            if (settings.directionalShadow.rayTraced) {
                EDITOR::PropertyLabel(TEXT::Localize("ui.quality.directionalShadow.rayTracedSamples").c_str());
                changed |= EDITOR::SliderNumber("##RayTracedShadowSamples", settings.directionalShadow.rayTracedSamples,
                                                1.0, 8.0, &kDefaults.directionalShadow.rayTracedSamples);
            }
            EDITOR::EndPropertyTable();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.quality.directionalShadow").c_str())) {
        changed |=
            EDITOR::DrawJsonFields(
                "ui.quality.directionalShadow", settings.directionalShadow, RENDER3D::kDirectionalShadowFields,
                [](std::string_view key, auto&, EDITOR::FieldEditResult&) { return key.starts_with("rayTraced"); })
                .changed;
        ImGui::TreePop();
    }

    if (changed) {
        RENDER3D::SetRenderQualitySettings(settings);
    }
    return beforeEdit != RENDER3D::GetRenderQualitySettings();
}

} // namespace HIKARI::EDITOR::QUALITY_PANEL
