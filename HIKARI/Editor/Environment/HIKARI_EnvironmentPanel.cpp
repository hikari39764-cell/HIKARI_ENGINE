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
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cstdint>
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

bool DrawSolarAngularRadius(float& radius) {
    float degrees = DirectX::XMConvertToDegrees(radius);
    if (!ImGui::DragFloat(TEXT::Localize("ui.environment.sunAngularRadius").c_str(), &degrees, 0.01f,
                          DirectX::XMConvertToDegrees(ATMOSPHERE::kMinSolarAngularRadius),
                          DirectX::XMConvertToDegrees(ATMOSPHERE::kMaxSolarAngularRadius), "%.3f"))
        return false;
    radius = DirectX::XMConvertToRadians(degrees);
    return true;
}

} // namespace

EnvironmentPanelResult EnvironmentPanel::Draw(SceneEnvironment& environment, const SKYRENDERER::SkyRendererDebugState*,
                                              const AssetRegistry* assetRegistry,
                                              const AssetDatabase* assetDatabase) const {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.environment", "Environment").c_str())) {
        ImGui::End();
        return {};
    }

    // 上から実行時の状態（エラーがあれば log を案内）、クイック操作、preset、各設定の順に並べる。
    const SceneEnvironment beforeEdit = environment;
    bool openLightingBakeRequested = false;

    ImGui::SeparatorText(TEXT::Localize("ui.environment.sceneEnvironment").c_str());
    const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
        RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);
    ImGui::TextDisabled(TEXT::Localize("ui.environment.runtimeFormat").c_str(),
                        RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
                        RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
                        runtimeSnapshot.recentRenderErrorCount);
    if (runtimeSnapshot.recentRenderErrorCount > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", TEXT::Localize("ui.environment.checkLog").c_str());
    }

    if (ImGui::TreeNodeEx(TEXT::Localize("ui.environment.quickControls").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat(TEXT::Localize("ui.environment.ambientIntensity").c_str(), &environment.ambient.intensity,
                         0.01f, 0.0f, 10.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.fogAmount").c_str(), &environment.fog.density, 0.001f, 0.0f,
                         1.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.presets").c_str())) {
        const int presetCount = static_cast<int>(std::size(kEnvironmentPresets));
        for (int i = 0; i < presetCount; ++i) {
            ImGui::PushID(i);
            if (ImGui::Button(TEXT::Localize(kEnvironmentPresets[i].labelKey).c_str())) {
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

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.sky").c_str())) {
        ImGui::Checkbox(TEXT::Localize("ui.environment.skyEnabled").c_str(), &environment.sky.enabled);
        int skyMode = static_cast<int>(environment.sky.mode);
        const std::array<std::string, 5> skyModeNames{
            TEXT::Localize("ui.environment.skyModeNone"),       TEXT::Localize("ui.environment.skyModeGradient"),
            TEXT::Localize("ui.environment.skyModeCubemap"),    TEXT::Localize("ui.environment.skyModeTexture2D"),
            TEXT::Localize("ui.environment.skyModeAtmosphere"),
        };
        const char* skyModeNamePtrs[5]{};
        for (size_t index = 0; index < skyModeNames.size(); ++index) {
            skyModeNamePtrs[index] = skyModeNames[index].c_str();
        };
        if (ImGui::Combo(TEXT::Localize("ui.environment.skyMode").c_str(), &skyMode, skyModeNamePtrs,
                         static_cast<int>(skyModeNames.size()))) {
            environment.sky.mode = static_cast<SkyMode>(std::clamp(skyMode, 0, 4));
        }
        if (environment.sky.mode == SkyMode::Texture2D) {
            DrawPanoramaTextureAssetPicker(assetDatabase, "ui.environment.panoramaBackgroundAsset",
                                           environment.sky.skyAsset);
        } else {
            DrawSkyAssetPicker(assetRegistry, assetDatabase, environment.sky.skyAsset);
        }
        ImGui::DragFloat(TEXT::Localize("ui.environment.skyScale").c_str(), &environment.sky.scale, 0.01f, 0.0001f,
                         1000.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.skyYaw").c_str(), &environment.sky.yaw, 0.01f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.skyIntensity").c_str(), &environment.sky.exposure, 0.01f, 0.0f,
                         16.0f);
        ImGui::ColorEdit3(TEXT::Localize("ui.environment.skyTint").c_str(), &environment.sky.tint.x);
        ImGui::Checkbox(TEXT::Localize("ui.environment.followCamera").c_str(), &environment.sky.followCamera);

        if (environment.sky.mode == SkyMode::Texture2D &&
            ImGui::TreeNode(TEXT::Localize("ui.environment.panoramaCloudLayer").c_str())) {
            ImGui::Checkbox(TEXT::Localize("ui.environment.panoramaCloudEnabled").c_str(),
                            &environment.sky.panoramaCloudEnabled);
            DrawPanoramaTextureAssetPicker(assetDatabase, "ui.environment.panoramaCloudAsset",
                                           environment.sky.panoramaCloudAsset);
            ImGui::DragFloat2(TEXT::Localize("ui.environment.panoramaCloudScrollSpeed").c_str(),
                              &environment.sky.panoramaCloudScrollSpeed.x, 0.0001f, -0.1f, 0.1f, "%.4f");
            ImGui::DragFloat(TEXT::Localize("ui.environment.panoramaCloudOpacity").c_str(),
                             &environment.sky.panoramaCloudOpacity, 0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (environment.sky.mode == SkyMode::Gradient || environment.sky.mode == SkyMode::Cubemap) {
            if (ImGui::TreeNode(TEXT::Localize("ui.environment.gradientFallback").c_str())) {
                ImGui::ColorEdit3(TEXT::Localize("ui.environment.zenithColor").c_str(), &environment.sky.zenithColor.x);
                ImGui::ColorEdit3(TEXT::Localize("ui.environment.horizonColor").c_str(),
                                  &environment.sky.horizonColor.x);
                ImGui::ColorEdit3(TEXT::Localize("ui.environment.groundColor").c_str(), &environment.sky.groundColor.x);
                ImGui::DragFloat(TEXT::Localize("ui.environment.horizonPower").c_str(), &environment.sky.horizonPower,
                                 0.01f, 0.01f, 8.0f);
                ImGui::TreePop();
            }
        }
        if (environment.sky.mode == SkyMode::Atmosphere) {
            if (ImGui::TreeNode(TEXT::Localize("ui.environment.atmosphere").c_str())) {
                AtmosphereSettings& atmosphere = environment.atmosphere;
                ImGui::Checkbox(TEXT::Localize("ui.environment.atmosphereEnabled").c_str(), &atmosphere.enabled);
                ImGui::DragFloat(TEXT::Localize("ui.environment.planetRadius").c_str(), &atmosphere.planetRadius,
                                 100.0f, 1.0f, 100000000.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.atmosphereHeight").c_str(),
                                 &atmosphere.atmosphereHeight, 100.0f, 1.0f, 1000000.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.rayleighScaleHeight").c_str(),
                                 &atmosphere.rayleighScaleHeight, 10.0f, 1.0f, 1000000.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.mieScaleHeight").c_str(), &atmosphere.mieScaleHeight,
                                 10.0f, 1.0f, 1000000.0f);
                ImGui::DragFloat3(TEXT::Localize("ui.environment.rayleighScattering").c_str(),
                                  &atmosphere.rayleighScattering.x, 0.0000001f, 0.0f, 0.001f, "%.8f");
                ImGui::DragFloat3(TEXT::Localize("ui.environment.mieScattering").c_str(), &atmosphere.mieScattering.x,
                                  0.0000001f, 0.0f, 0.001f, "%.8f");
                ImGui::DragFloat3(TEXT::Localize("ui.environment.ozoneAbsorption").c_str(),
                                  &atmosphere.ozoneAbsorption.x, 0.0000001f, 0.0f, 0.001f, "%.8f");
                ImGui::DragFloat(TEXT::Localize("ui.environment.mieAnisotropy").c_str(), &atmosphere.mieAnisotropy,
                                 0.005f, -0.95f, 0.95f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.ozoneCenterHeight").c_str(),
                                 &atmosphere.ozoneCenterHeight, 50.0f, 0.0f, 1000000.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.ozoneWidth").c_str(), &atmosphere.ozoneWidth, 50.0f,
                                 1.0f, 1000000.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.sunIlluminance").c_str(), &atmosphere.sunIlluminance,
                                 0.01f, 0.0f, 16.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.sunExposure").c_str(), &atmosphere.sunExposure, 0.01f,
                                 -16.0f, 16.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.groundAlbedo").c_str(), &atmosphere.groundAlbedo, 0.01f,
                                 0.0f, 1.0f);
                ImGui::DragFloat(TEXT::Localize("ui.environment.aerialPerspectiveStrength").c_str(),
                                 &atmosphere.aerialPerspectiveStrength, 0.01f, 0.0f, 8.0f);
                DrawSolarAngularRadius(atmosphere.sunAngularRadius);
                ImGui::Checkbox(TEXT::Localize("ui.environment.preferAtmosphereLut").c_str(), &atmosphere.preferLut);
                const uint32_t lutMin = 1u;
                const uint32_t lutMax = 4096u;
                ImGui::DragScalar(TEXT::Localize("ui.environment.transmittanceLutWidth").c_str(), ImGuiDataType_U32,
                                  &atmosphere.transmittanceLutWidth, 1.0f, &lutMin, &lutMax);
                ImGui::DragScalar(TEXT::Localize("ui.environment.transmittanceLutHeight").c_str(), ImGuiDataType_U32,
                                  &atmosphere.transmittanceLutHeight, 1.0f, &lutMin, &lutMax);
                ImGui::DragScalar(TEXT::Localize("ui.environment.multiScatteringLutSize").c_str(), ImGuiDataType_U32,
                                  &atmosphere.multiScatteringLutSize, 1.0f, &lutMin, &lutMax);
                ImGui::DragScalar(TEXT::Localize("ui.environment.skyViewLutWidth").c_str(), ImGuiDataType_U32,
                                  &atmosphere.skyViewLutWidth, 1.0f, &lutMin, &lutMax);
                ImGui::DragScalar(TEXT::Localize("ui.environment.skyViewLutHeight").c_str(), ImGuiDataType_U32,
                                  &atmosphere.skyViewLutHeight, 1.0f, &lutMin, &lutMax);
                ImGui::TextDisabled("%s", TEXT::Localize("ui.environment.atmosphereLutOwnerHint").c_str());
                ImGui::TreePop();
            }
        }
        if (ImGui::TreeNode(TEXT::Localize("ui.environment.sunDisk").c_str())) {
            ImGui::Checkbox(TEXT::Localize("ui.environment.showSunDisk").c_str(), &environment.sky.showSunDisk);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunDiskIntensity").c_str(),
                             &environment.sky.sunDiskIntensity, 0.01f, 0.0f, 32.0f);
            if (environment.sky.mode != SkyMode::Atmosphere) {
                float radius = ATMOSPHERE::SolarAngularRadiusFromDiskSize(environment.sky.sunDiskSize);
                if (DrawSolarAngularRadius(radius))
                    environment.sky.sunDiskSize = ATMOSPHERE::SolarDiskSizeFromAngularRadius(radius);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode(TEXT::Localize("ui.environment.sunGlare").c_str())) {
            SunGlareSettings& glare = environment.sunGlare;
            ImGui::Checkbox(TEXT::Localize("ui.environment.sunGlareEnabled").c_str(), &glare.enabled);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareIntensity").c_str(), &glare.intensity, 0.01f, 0.0f,
                             8.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareHaloRadius").c_str(), &glare.haloRadius, 0.005f,
                             0.0f, 2.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareHaloFalloff").c_str(), &glare.haloFalloff, 0.05f,
                             0.1f, 16.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareStreakIntensity").c_str(), &glare.streakIntensity,
                             0.01f, 0.0f, 8.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareStreakLength").c_str(), &glare.streakLength, 0.01f,
                             0.0f, 2.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareOcclusionRadius").c_str(), &glare.occlusionRadius,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareEdgeFade").c_str(), &glare.edgeFade, 0.01f, 0.0f,
                             1.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunGlareMaxRadiance").c_str(), &glare.maxRadiance, 0.05f,
                             0.0f, 64.0f);
            ImGui::SeparatorText(TEXT::Localize("ui.environment.sunShafts").c_str());
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunShaftIntensity").c_str(), &glare.shaftIntensity, 0.01f,
                             0.0f, 8.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.sunShaftLength").c_str(), &glare.shaftLength, 0.01f, 0.01f,
                             2.0f);
            ImGui::SliderFloat(TEXT::Localize("ui.environment.sunShaftDecay").c_str(), &glare.shaftDecay, 0.5f, 0.999f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode(TEXT::Localize("ui.environment.environmentOutput").c_str())) {
            ImGui::DragFloat(TEXT::Localize("ui.environment.ambientFromSky").c_str(), &environment.sky.ambientFromSky,
                             0.01f, 0.0f, 8.0f);
            ImGui::DragFloat(TEXT::Localize("ui.environment.reflectionIntensity").c_str(),
                             &environment.sky.reflectionIntensity, 0.01f, 0.0f, 8.0f);
            ImGui::Checkbox(TEXT::Localize("ui.environment.useSkyColorForAmbient").c_str(),
                            &environment.ambient.useSkyColor);
            ImGui::DragFloat(TEXT::Localize("ui.environment.skyAmbientBlend").c_str(), &environment.ambient.skyBlend,
                             0.01f, 0.0f, 1.0f);
            ImGui::Checkbox(TEXT::Localize("ui.environment.useSkyHorizonForFog").c_str(),
                            &environment.fog.useSkyHorizonColor);
            if (ImGui::Button(TEXT::Localize("ui.environment.applyHorizonToFog").c_str())) {
                environment.fog.color = environment.sky.horizonColor;
            }
            ImGui::TreePop();
        }
        if (environment.sky.mode == SkyMode::Cubemap) {
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "%s",
                               TEXT::Localize("ui.environment.cubemapRequiresDds").c_str());
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "%s",
                               TEXT::Localize("ui.environment.panoramaUseTexture2D").c_str());
        }
        ImGui::Checkbox(TEXT::Localize("ui.environment.showSkyDebugTexture").c_str(),
                        &environment.sky.showDebugTexture);
        ImGui::TextDisabled(TEXT::Localize("ui.environment.runtimeSkyFormat").c_str(),
                            RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot));
        ImGui::TextDisabled(TEXT::Localize("ui.environment.runtimeIblFormat").c_str(),
                            RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot));
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.lightingObjects").c_str())) {
        ImGui::TextWrapped("%s", TEXT::Localize("ui.environment.lightingObjectsHint").c_str());
        if (ImGui::Button(TEXT::Localize("ui.environment.openLightingBakeTool").c_str())) {
            openLightingBakeRequested = true;
        }
        ImGui::TextDisabled(TEXT::Localize("ui.environment.primaryProbeFormat").c_str(),
                            RENDER3D::DIAGNOSTICS::ResolveReflectionProbeSummaryLabel(runtimeSnapshot));
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.clouds").c_str())) {
        StylizedCloudSettings& clouds = environment.clouds;
        ImGui::Checkbox(TEXT::Localize("ui.environment.cloudsEnabled").c_str(), &clouds.enabled);
        ImGui::Checkbox(TEXT::Localize("ui.environment.cloudPhysicalLighting").c_str(),
                        &clouds.physicallyBasedLighting);
        if (clouds.physicallyBasedLighting)
            ImGui::SliderFloat(TEXT::Localize("ui.environment.cloudAnisotropy").c_str(), &clouds.anisotropy, -0.8f,
                               0.8f);
        ImGui::SliderFloat(TEXT::Localize("ui.environment.cloudCoverage").c_str(), &clouds.coverage, 0.0f, 1.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudDensity").c_str(), &clouds.density, 0.01f, 0.0f, 8.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudAltitude").c_str(), &clouds.altitude, 1.0f, 0.0f,
                         100000.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudThickness").c_str(), &clouds.thickness, 1.0f, 0.1f,
                         100000.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudScale").c_str(), &clouds.scale, 1.0f, 0.1f, 100000.0f);
        ImGui::DragFloat2(TEXT::Localize("ui.environment.cloudWindDirection").c_str(), &clouds.windDirection.x, 0.01f,
                          -1.0f, 1.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudWindSpeed").c_str(), &clouds.windSpeed, 0.01f, 0.0f,
                         1000.0f);
        ImGui::SliderFloat(TEXT::Localize("ui.environment.cloudShapeDetail").c_str(), &clouds.shapeDetail, 0.0f, 1.0f);
        ImGui::SliderFloat(TEXT::Localize("ui.environment.cloudSoftness").c_str(), &clouds.softness, 0.0f, 1.0f);
        ImGui::BeginDisabled(clouds.physicallyBasedLighting);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudLightAbsorption").c_str(), &clouds.lightAbsorption, 0.01f,
                         0.0f, 8.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.cloudSilverLining").c_str(), &clouds.silverLining, 0.01f, 0.0f,
                         4.0f);
        ImGui::EndDisabled();
        ImGui::SliderFloat(TEXT::Localize("ui.environment.cloudHorizonFade").c_str(), &clouds.horizonFade, 0.0f, 1.0f);
        ImGui::ColorEdit3(TEXT::Localize(clouds.physicallyBasedLighting ? "ui.environment.cloudAlbedo"
                                                                        : "ui.environment.cloudBrightColor")
                              .c_str(),
                          &clouds.brightColor.x);
        ImGui::BeginDisabled(clouds.physicallyBasedLighting);
        ImGui::ColorEdit3(TEXT::Localize("ui.environment.cloudShadowColor").c_str(), &clouds.shadowColor.x);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.fog").c_str())) {
        ImGui::Checkbox(TEXT::Localize("ui.environment.fogEnabled").c_str(), &environment.fog.enabled);
        ImGui::Checkbox(TEXT::Localize("ui.environment.volumetricFog").c_str(), &environment.fog.volumetric);
        ImGui::ColorEdit3(TEXT::Localize("ui.environment.fogColor").c_str(), &environment.fog.color.x);
        ImGui::DragFloat(TEXT::Localize("ui.environment.density").c_str(), &environment.fog.density, 0.001f, 0.0f,
                         1.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.startDistance").c_str(), &environment.fog.startDistance, 0.1f,
                         0.0f, 500.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.endDistance").c_str(), &environment.fog.endDistance, 0.1f, 0.1f,
                         1000.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.heightFalloff").c_str(), &environment.fog.heightFalloff, 0.001f,
                         0.0f, 2.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.fogBaseHeight").c_str(), &environment.fog.baseHeight, 0.1f,
                         -10000.0f, 10000.0f);
        ImGui::SliderFloat(TEXT::Localize("ui.environment.anisotropy").c_str(), &environment.fog.anisotropy, -0.8f,
                           0.8f);
        ImGui::SliderFloat(TEXT::Localize("ui.environment.temporalWeight").c_str(), &environment.fog.temporalWeight,
                           0.0f, 0.98f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.volumetricScatteringIntensity").c_str(),
                         &environment.fog.scatteringIntensity, 0.01f, 0.0f, 8.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.volumetricAmbientStrength").c_str(),
                         &environment.fog.ambientScatteringStrength, 0.01f, 0.0f, 8.0f);
        ImGui::DragFloat(TEXT::Localize("ui.environment.volumetricSunShaftStrength").c_str(),
                         &environment.fog.sunShaftStrength, 0.01f, 0.0f, 8.0f);
        ImGui::Checkbox(TEXT::Localize("ui.environment.useSkyHorizonColor").c_str(),
                        &environment.fog.useSkyHorizonColor);
        if (ImGui::Button(TEXT::Localize("ui.environment.setFogFromSkyHorizon").c_str())) {
            environment.fog.color = environment.sky.horizonColor;
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::Localize("ui.environment.debug").c_str())) {
        ImGui::Checkbox(TEXT::Localize("ui.environment.showLightDebug").c_str(), &environment.showLightDebug);
        ImGui::Checkbox(TEXT::Localize("ui.environment.showSkyDebugInfo").c_str(), &environment.showSkyDebugInfo);

        ImGui::TextDisabled(TEXT::Localize("ui.environment.runtimeFormat").c_str(),
                            RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
                            RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
                            runtimeSnapshot.recentRenderErrorCount);
        if (runtimeSnapshot.recentRenderErrorCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s",
                               TEXT::Localize("ui.environment.renderDiagnosticsErrors").c_str());
        }

        // Debug dump 驍ｵ・ｺ繝ｻ・ｯ scene dirty
        // 驍ｵ・ｺ繝ｻ・ｫ驍ｵ・ｺ陷会ｽｱ遶企・・ｸ・ｺ郢晢ｽｻ・つ郢晢ｽｻ
        if (ImGui::Button(TEXT::Localize("ui.environment.dumpEnvironmentDiagnostics").c_str())) {
            RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshot("EnvironmentPanel", &environment);
        }
        ImGui::TreePop();
    }

    if (ImGui::Button(TEXT::Localize("ui.environment.resetDefaults").c_str())) {
        environment = SceneEnvironment{};
    }

    const bool changed = !EDITOR::ENVIRONMENT_EDIT::AreSceneEnvironmentValuesEqual(beforeEdit, environment);
    const bool editActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsAnyItemActive();
    ImGui::End();
    return EnvironmentPanelResult{changed, openLightingBakeRequested, editActive};
}
#else
EnvironmentPanelResult EnvironmentPanel::Draw(SceneEnvironment&, const SKYRENDERER::SkyRendererDebugState*,
                                              const AssetRegistry*, const AssetDatabase*) const {
    return {};
}
#endif

} // namespace HIKARI
