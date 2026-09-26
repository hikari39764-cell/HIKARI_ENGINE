#include "Editor/Quality/HIKARI_QualityPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <array>
#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
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

void DrawDirectionalShadow(SceneEnvironment& environment) {
    ImGui::Checkbox(TEXT::Localize("ui.quality.shadowEnabled").c_str(), &environment.directionalShadow.enabled);
    int shadowQuality = environment.directionalShadow.resolution <= 1024
                            ? 0
                            : (environment.directionalShadow.resolution >= 4096 ? 2 : 1);
    const std::array<std::string, 3> qualityNames{
        TEXT::Localize("ui.common.low"),
        TEXT::Localize("ui.common.medium"),
        TEXT::Localize("ui.common.high"),
    };
    const char* qualityNamePtrs[3]{};
    for (size_t index = 0; index < qualityNames.size(); ++index) {
        qualityNamePtrs[index] = qualityNames[index].c_str();
    };
    if (ImGui::Combo(TEXT::Localize("ui.quality.shadowQuality").c_str(), &shadowQuality, qualityNamePtrs,
                     static_cast<int>(qualityNames.size()))) {
        environment.directionalShadow.resolution = shadowQuality == 0 ? 1024u : (shadowQuality == 2 ? 4096u : 2048u);
    }
    int cascadeCount = static_cast<int>(environment.directionalShadow.cascadeCount);
    if (ImGui::SliderInt(TEXT::Localize("ui.quality.cascades").c_str(), &cascadeCount, 1, 4)) {
        environment.directionalShadow.cascadeCount = static_cast<uint32_t>(std::clamp(cascadeCount, 1, 4));
    }
    ImGui::DragFloat(TEXT::Localize("ui.quality.shadowSoftness").c_str(), &environment.directionalShadow.pcfRadius,
                     0.05f, 0.0f, 4.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.shadowDistance").c_str(), &environment.directionalShadow.shadowDistance,
                     0.1f, 1.0f, 200.0f);

    if (ImGui::TreeNode(TEXT::Localize("ui.quality.advancedShadowParameters").c_str())) {
        const int resolutions[] = {1024, 2048, 4096};
        int currentResolution = static_cast<int>(environment.directionalShadow.resolution);
        if (currentResolution != 1024 && currentResolution != 2048 && currentResolution != 4096) {
            currentResolution = 2048;
        }
        if (ImGui::BeginCombo(TEXT::Localize("ui.quality.resolution").c_str(),
                              std::to_string(currentResolution).c_str())) {
            for (int resolution : resolutions) {
                const bool selected = currentResolution == resolution;
                if (ImGui::Selectable(std::to_string(resolution).c_str(), selected)) {
                    environment.directionalShadow.resolution = static_cast<uint32_t>(resolution);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat(TEXT::Localize("ui.quality.cascadeSplitBalance").c_str(),
                         &environment.directionalShadow.cascadeSplitLambda, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.cascadeBlend").c_str(), &environment.directionalShadow.cascadeBlend,
                         0.005f, 0.0f, 0.3f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.nearPlane").c_str(), &environment.directionalShadow.nearPlane,
                         0.01f, 0.001f, 50.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.farPlane").c_str(), &environment.directionalShadow.farPlane, 0.1f,
                         1.0f, 500.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.depthBias").c_str(), &environment.directionalShadow.depthBias,
                         0.0001f, 0.0f, 0.05f, "%.5f");
        ImGui::DragFloat(TEXT::Localize("ui.quality.normalBias").c_str(), &environment.directionalShadow.normalBias,
                         0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat(TEXT::Localize("ui.quality.stylizedShadowStrength").c_str(),
                         &environment.directionalShadow.strength, 0.01f, 0.0f, 1.0f);
        ImGui::Checkbox(TEXT::Localize("ui.quality.pcfEnabled").c_str(), &environment.directionalShadow.pcfEnabled);
        ImGui::DragFloat(TEXT::Localize("ui.quality.pcfRadius").c_str(), &environment.directionalShadow.pcfRadius,
                         0.05f, 0.0f, 4.0f);
        ImGui::Checkbox(TEXT::Localize("ui.quality.stabilize").c_str(), &environment.directionalShadow.stabilize);
        if (ImGui::TreeNode(TEXT::Localize("ui.quality.debugVisualization").c_str())) {
            ImGui::Checkbox(TEXT::Localize("ui.quality.showDebugTexture").c_str(),
                            &environment.directionalShadow.showDebugTexture);
            ImGui::Checkbox(TEXT::Localize("ui.quality.showCascadeFrusta").c_str(),
                            &environment.directionalShadow.showDebugFrustum);
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

void DrawScreenSpaceReflections(SceneEnvironment& environment) {
    ScreenSpaceReflectionSettings& settings = environment.screenSpaceReflections;
    ImGui::Checkbox(TEXT::Localize("ui.quality.ssrEnabled").c_str(), &settings.enabled);
    int quality = static_cast<int>(settings.quality);
    const std::array<std::string, 4> qualityNames{
        TEXT::Localize("ui.common.off"),
        TEXT::Localize("ui.common.low"),
        TEXT::Localize("ui.common.medium"),
        TEXT::Localize("ui.common.high"),
    };
    const char* qualityNamePtrs[4]{};
    for (size_t index = 0; index < qualityNames.size(); ++index) {
        qualityNamePtrs[index] = qualityNames[index].c_str();
    };
    if (ImGui::Combo(TEXT::Localize("ui.quality.quality").c_str(), &quality, qualityNamePtrs,
                     static_cast<int>(qualityNames.size()))) {
        settings.quality = static_cast<ScreenSpaceReflectionQuality>(std::clamp(quality, 0, 3));
        settings.enabled = settings.quality != ScreenSpaceReflectionQuality::Off;
    }
    ImGui::DragFloat(TEXT::Localize("ui.quality.intensity").c_str(), &settings.intensity, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.traceDistance").c_str(), &settings.maxDistance, 0.25f, 1.0f, 200.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.maximumRoughness").c_str(), &settings.maxRoughness, 0.01f, 0.05f, 1.0f);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.ssrFallback").c_str());
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.ssrScreenHits").c_str());
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.ssrTraceResolution").c_str());

    if (ImGui::TreeNode(TEXT::Localize("ui.quality.advancedRayMarching").c_str())) {
        ImGui::DragFloat(TEXT::Localize("ui.quality.relativeDepthThickness").c_str(), &settings.thickness, 0.0001f,
                         0.0001f, 0.05f, "%.5f");
        ImGui::DragFloat(TEXT::Localize("ui.quality.rayStride").c_str(), &settings.stride, 0.01f, 0.02f, 2.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.screenEdgeFade").c_str(), &settings.edgeFade, 0.005f, 0.01f, 0.35f);
        ImGui::TreePop();
    }
}

void DrawSurfaceLightmaps(SceneEnvironment& environment) {
    ImGui::Checkbox(TEXT::Localize("ui.quality.useSurfaceLightmaps").c_str(), &environment.surfaceLightmaps.enabled);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.surfaceLightmapHint1").c_str());
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.surfaceLightmapHint2").c_str());
}

void DrawAmbientOcclusion(SceneEnvironment& environment,
                          const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot& runtimeSnapshot) {
    int ssaoModeIndex = EDITOR::ENVIRONMENT_EDIT::ToSsaoModeIndex(environment.ambientOcclusion);
    const std::array<std::string, 4> ssaoModeNames{
        TEXT::Localize("ui.common.off"),
        TEXT::Localize("ui.quality.ssaoReference"),
        TEXT::Localize("ui.quality.ssaoOptimizedHigh"),
        TEXT::Localize("ui.quality.ssaoBalanced"),
    };
    const char* ssaoModeNamePtrs[4]{};
    for (size_t index = 0; index < ssaoModeNames.size(); ++index) {
        ssaoModeNamePtrs[index] = ssaoModeNames[index].c_str();
    };
    if (ImGui::Combo(TEXT::Localize("ui.quality.ssaoMode").c_str(), &ssaoModeIndex, ssaoModeNamePtrs,
                     static_cast<int>(ssaoModeNames.size()))) {
        environment.ambientOcclusion.mode = EDITOR::ENVIRONMENT_EDIT::SsaoModeFromIndex(ssaoModeIndex);
        environment.ambientOcclusion.enabled = environment.ambientOcclusion.mode != SsaoMode::Off;
    }
    ImGui::DragFloat(TEXT::Localize("ui.quality.radius").c_str(), &environment.ambientOcclusion.radius, 0.01f, 0.01f,
                     10.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.bias").c_str(), &environment.ambientOcclusion.bias, 0.001f, 0.0f, 0.5f,
                     "%.4f");
    ImGui::DragFloat(TEXT::Localize("ui.quality.strength").c_str(), &environment.ambientOcclusion.strength, 0.01f, 0.0f,
                     4.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.power").c_str(), &environment.ambientOcclusion.power, 0.01f, 0.1f,
                     8.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.diffuseStrength").c_str(),
                     &environment.ambientOcclusion.diffuseStrength, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.specularStrength").c_str(),
                     &environment.ambientOcclusion.specularStrength, 0.01f, 0.0f, 1.0f);

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
    const char* sampleNames[] = {"8", "16", "24", "32"};
    if (ImGui::Combo(TEXT::Localize("ui.quality.samples").c_str(), &sampleIndex, sampleNames,
                     static_cast<int>(std::size(sampleNames)))) {
        const uint32_t sampleValues[] = {8u, 16u, 24u, 32u};
        environment.ambientOcclusion.sampleCount = sampleValues[std::clamp(sampleIndex, 0, 3)];
    }
    int blurIterations = static_cast<int>(environment.ambientOcclusion.blurIterations);
    if (ImGui::SliderInt(TEXT::Localize("ui.quality.blurIterations").c_str(), &blurIterations, 0, 4)) {
        environment.ambientOcclusion.blurIterations = static_cast<uint32_t>(std::clamp(blurIterations, 0, 4));
    }
    ImGui::TextDisabled(TEXT::Localize("ui.quality.runtimeAoFormat").c_str(),
                        RENDER3D::DIAGNOSTICS::ResolveSsaoSummaryLabel(runtimeSnapshot));
}

void DrawBloom(SceneEnvironment& environment) {
    ImGui::Checkbox(TEXT::Localize("ui.quality.bloomEnabled").c_str(), &environment.bloom.enabled);
    ImGui::DragFloat(TEXT::Localize("ui.quality.threshold").c_str(), &environment.bloom.threshold, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.intensity").c_str(), &environment.bloom.intensity, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.radius").c_str(), &environment.bloom.radius, 0.01f, 0.0f, 8.0f);
    int downsampleCount = static_cast<int>(environment.bloom.downsampleCount);
    if (ImGui::SliderInt(TEXT::Localize("ui.quality.downsampleCount").c_str(), &downsampleCount, 1, 5)) {
        environment.bloom.downsampleCount = static_cast<uint32_t>(std::clamp(downsampleCount, 1, 5));
    }
}

void DrawToneMapping(SceneEnvironment& environment) {
    ImGui::Checkbox(TEXT::Localize("ui.quality.toneCurveEnabled").c_str(), &environment.toneMapping.enabled);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.toneCurveHint").c_str());
    int exposureMode = environment.toneMapping.exposureMode == RENDER3D::COLOR::ExposureMode::Automatic ? 1 : 0;
    const std::array<std::string, 2> exposureModes{
        TEXT::Localize("ui.quality.exposureManual"),
        TEXT::Localize("ui.quality.exposureAutomatic"),
    };
    const char* exposureModePtrs[2]{};
    for (size_t index = 0; index < exposureModes.size(); ++index) {
        exposureModePtrs[index] = exposureModes[index].c_str();
    };
    if (ImGui::Combo(TEXT::Localize("ui.quality.exposureMode").c_str(), &exposureMode, exposureModePtrs,
                     static_cast<int>(exposureModes.size()))) {
        environment.toneMapping.exposureMode =
            exposureMode == 1 ? RENDER3D::COLOR::ExposureMode::Automatic : RENDER3D::COLOR::ExposureMode::Manual;
    }
    const std::string exposureLabel = environment.toneMapping.exposureMode == RENDER3D::COLOR::ExposureMode::Automatic
                                          ? TEXT::Localize("ui.quality.exposureSeed")
                                          : TEXT::Localize("ui.quality.manualExposure");
    ImGui::DragFloat(exposureLabel.c_str(), &environment.toneMapping.exposure, 0.01f, 0.01f, 16.0f);
    if (environment.toneMapping.exposureMode == RENDER3D::COLOR::ExposureMode::Automatic) {
        auto& automatic = environment.toneMapping.automaticExposure;
        ImGui::Indent();
        ImGui::DragFloat(TEXT::Localize("ui.quality.targetGrey").c_str(), &automatic.targetLuminance, 0.001f, 0.02f,
                         0.5f, "%.3f");
        ImGui::DragFloat(TEXT::Localize("ui.quality.exposureCompensationEv").c_str(), &automatic.compensationEv, 0.05f,
                         -8.0f, 8.0f, "%+.2f");
        ImGui::DragFloat(TEXT::Localize("ui.quality.minimumExposureEv").c_str(), &automatic.minExposureEv, 0.05f,
                         -16.0f, automatic.maxExposureEv);
        ImGui::DragFloat(TEXT::Localize("ui.quality.maximumExposureEv").c_str(), &automatic.maxExposureEv, 0.05f,
                         automatic.minExposureEv, 16.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.brightAdaptSpeed").c_str(), &automatic.speedUp, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat(TEXT::Localize("ui.quality.darkAdaptSpeed").c_str(), &automatic.speedDown, 0.05f, 0.0f, 20.0f);
        const auto& runtimeExposure = POST::PostSystem::GetAutomaticExposureDebugState();
        if (runtimeExposure.measurementValid) {
            ImGui::TextDisabled(TEXT::Localize("ui.quality.meterFormat").c_str(), runtimeExposure.meteredLuminance,
                                runtimeExposure.currentExposure, runtimeExposure.targetExposure);
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.waitingForLuminance").c_str());
        }
        ImGui::Unindent();
    }
    ImGui::SeparatorText(TEXT::Localize("ui.quality.displayColor").c_str());
    ImGui::DragFloat(TEXT::Localize("ui.quality.whiteBalanceK").c_str(),
                     &environment.toneMapping.whiteBalanceTemperature, 25.0f, 2000.0f, 15000.0f, "%.0f K");
    ImGui::DragFloat(TEXT::Localize("ui.quality.whiteBalanceTint").c_str(), &environment.toneMapping.whiteBalanceTint,
                     0.01f, -1.0f, 1.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.contrast").c_str(), &environment.toneMapping.contrast, 0.01f, 0.25f,
                     2.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.saturation").c_str(), &environment.toneMapping.saturation, 0.01f, 0.0f,
                     2.0f);
    ImGui::DragFloat(TEXT::Localize("ui.quality.sdrDisplayGamma").c_str(), &environment.toneMapping.gamma, 0.01f, 0.1f,
                     4.0f);
    const std::array<std::string, 3> modes{
        TEXT::Localize("ui.common.none"),
        TEXT::Localize("ui.quality.reinhard"),
        TEXT::Localize("ui.quality.acesFitted"),
    };
    const char* modePtrs[3]{};
    for (size_t index = 0; index < modes.size(); ++index) {
        modePtrs[index] = modes[index].c_str();
    };
    int mode = std::clamp(environment.toneMapping.mode, 0, 2);
    if (ImGui::Combo(TEXT::Localize("ui.quality.mode").c_str(), &mode, modePtrs, static_cast<int>(modes.size()))) {
        environment.toneMapping.mode = mode;
    }
}

void DrawTransitionDebug() {
    const SceneTransitionBus* transitionBus = RuntimeSceneContext::GetTransitionBus();
    if (!transitionBus) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.transitionBusUnavailable").c_str());
        return;
    }

    const TransitionVisualState visualState = transitionBus->GetVisualState();
    const SceneTransitionBus::TransitionState state = transitionBus->GetState();
    std::string stateLabel = TEXT::Localize("ui.quality.transitionUnknown");
    switch (state) {
    case SceneTransitionBus::TransitionState::Idle: stateLabel = TEXT::Localize("ui.quality.transitionIdle"); break;
    case SceneTransitionBus::TransitionState::TransitionOut:
        stateLabel = TEXT::Localize("ui.quality.transitionOut");
        break;
    case SceneTransitionBus::TransitionState::SwitchingScene:
        stateLabel = TEXT::Localize("ui.quality.transitionSwitching");
        break;
    case SceneTransitionBus::TransitionState::TransitionIn:
        stateLabel = TEXT::Localize("ui.quality.transitionIn");
        break;
    default: break;
    }

    ImGui::Text(TEXT::Localize("ui.quality.profileIdFormat").c_str(), visualState.profileId.empty()
                                                                          ? TEXT::Localize("ui.common.none").c_str()
                                                                          : visualState.profileId.c_str());
    ImGui::Text(TEXT::Localize("ui.quality.stateFormat").c_str(), stateLabel.c_str());
    ImGui::Text(TEXT::Localize("ui.quality.progressFormat").c_str(), visualState.progress);
    ImGui::Text(TEXT::Localize("ui.quality.outDurationFormat").c_str(), visualState.outDuration);
    ImGui::Text(TEXT::Localize("ui.quality.inDurationFormat").c_str(), visualState.inDuration);
}
#endif

} // namespace HIKARI::EDITOR::QUALITY_PANEL
