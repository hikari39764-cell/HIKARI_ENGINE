#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"

#include <algorithm>
#include <cmath>

#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

namespace HIKARI::RENDER3D {

namespace {
RenderQualitySettings gRenderQualitySettings{};

RenderResolution ClampResolution(RenderResolution resolution) {
    resolution.width = std::clamp(resolution.width, 16, 8192);
    resolution.height = std::clamp(resolution.height, 16, 8192);
    return resolution;
}
} // namespace

RenderQualitySettings& GetRenderQualitySettings() { return gRenderQualitySettings; }

RenderQualitySettings NormalizeRenderQualitySettings(RenderQualitySettings settings) {
    // 数値の範囲は項目表の Range（入れ子の太陽の影も同じ表）。
    settings = JSONFIELDS::Sanitize(settings, kRenderQualitySettingsFields);
    DirectionalShadowQualitySettings& shadow = settings.directionalShadow;
    shadow.farPlane = (std::max)(shadow.farPlane, shadow.nearPlane + 1.0f);
    // 表に無い値は既定へ戻す。窓の大きさは固定の大きさだけ（視口の大きさは場面の解像度だけが取る）。
    const auto named = [](auto& value, const auto& names, auto fallback) {
        if (!TEXT::IsNamedEnum(names, value)) {
            value = fallback;
        }
    };
    named(settings.sceneResolution, kRenderResolutionPresetNames, RenderResolutionPreset::P1080);
    if (!IsFixedRenderResolutionPreset(settings.windowSize) ||
        !TEXT::IsNamedEnum(kRenderResolutionPresetNames, settings.windowSize)) {
        settings.windowSize = RenderResolutionPreset::P720;
    }
    named(settings.windowMode, kWindowPresentationModeNames, WindowPresentationMode::Windowed);
    named(settings.geometryPipeline, kGeometryPipelineModeNames, GeometryPipelineMode::MeshShader);
    named(settings.antiAliasingMode, kRenderAntiAliasingModeNames, RenderAntiAliasingMode::Off);
    named(settings.dlssQualityMode, kDlssQualityModeNames, DlssQualityMode::Quality);
    named(settings.frameGenerationMode, kRenderFrameGenerationModeNames, RenderFrameGenerationMode::Off);
    named(settings.volumetricLightingQuality, kVolumetricLightingQualityNames, VolumetricLightingQuality::Balanced);
    named(settings.depthOfFieldQuality, kDepthOfFieldQualityNames, DepthOfFieldQuality::Medium);
    named(settings.motionBlurQuality, kMotionBlurQualityNames, MotionBlurQuality::Medium);
    named(settings.dynamicGiQuality, kDynamicGiQualityNames, DynamicGiQuality::Off);
    named(settings.localShadowQuality, kLocalShadowQualityNames, LocalShadowQuality::Balanced);
    named(settings.volumetricCloudQuality, kVolumetricCloudQualityNames, VolumetricCloudQuality::Balanced);
    named(settings.fluidSmokeQuality, kFluidSmokeQualityNames, FluidSmokeQuality::Balanced);
    named(settings.forwardCostMode, kForwardShadingCostModeNames, ForwardShadingCostMode::Full);
    named(settings.lightProbeVolumeSampling, kLightProbeVolumeSamplingModeNames,
          LightProbeVolumeSamplingMode::FastSmooth);
    return settings;
}

RenderQualityRuntimeChangeSet ClassifyRenderQualityRuntimeChanges(const RenderQualitySettings& before,
                                                                  const RenderQualitySettings& after) {
    const RenderQualitySettings normalizedBefore = NormalizeRenderQualitySettings(before);
    const RenderQualitySettings normalizedAfter = NormalizeRenderQualitySettings(after);
    const bool beforeUsesStreamline = IsDlaaAntiAliasingMode(normalizedBefore.antiAliasingMode) ||
                                      IsDlssAntiAliasingMode(normalizedBefore.antiAliasingMode);
    const bool afterUsesStreamline = IsDlaaAntiAliasingMode(normalizedAfter.antiAliasingMode) ||
                                     IsDlssAntiAliasingMode(normalizedAfter.antiAliasingMode);

    RenderQualityRuntimeChangeSet changes{};
    changes.sceneResolutionChanged = normalizedBefore.sceneResolution != normalizedAfter.sceneResolution ||
                                     normalizedBefore.viewportScale != normalizedAfter.viewportScale;
    changes.temporalBackendChanged = normalizedBefore.antiAliasingMode != normalizedAfter.antiAliasingMode ||
                                     (IsDlssAntiAliasingMode(normalizedBefore.antiAliasingMode) &&
                                      IsDlssAntiAliasingMode(normalizedAfter.antiAliasingMode) &&
                                      normalizedBefore.dlssQualityMode != normalizedAfter.dlssQualityMode);
    changes.streamlineStateChanged = (beforeUsesStreamline || afterUsesStreamline) &&
                                     (changes.sceneResolutionChanged || changes.temporalBackendChanged ||
                                      normalizedBefore.dlssQualityMode != normalizedAfter.dlssQualityMode);
    changes.frameGenerationChanged =
        normalizedBefore.frameGenerationMode != normalizedAfter.frameGenerationMode ||
        normalizedBefore.frameGenerationMultiplier != normalizedAfter.frameGenerationMultiplier;
    changes.presentationChanged = normalizedBefore.windowSize != normalizedAfter.windowSize ||
                                  normalizedBefore.windowMode != normalizedAfter.windowMode;
    return changes;
}

void SetRenderQualitySettings(const RenderQualitySettings& settings) {
    gRenderQualitySettings = NormalizeRenderQualitySettings(settings);
}

const char* RenderResolutionPresetLabel(RenderResolutionPreset preset) {
    return TEXT::EnumToName(kRenderResolutionPresetLabels, preset, "Unknown");
}

const char* WindowPresentationModeLabel(WindowPresentationMode mode) {
    return TEXT::EnumToName(kWindowPresentationModeNames, mode, "Unknown");
}

const char* GeometryPipelineModeLabel(GeometryPipelineMode mode) {
    return TEXT::EnumToName(kGeometryPipelineModeLabels, mode, "Unknown");
}

const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode) {
    return TEXT::EnumToName(kForwardShadingCostModeLabels, mode, "Unknown");
}

const char* LightProbeVolumeSamplingModeLabel(LightProbeVolumeSamplingMode mode) {
    return TEXT::EnumToName(kLightProbeVolumeSamplingModeLabels, mode, "Unknown");
}

const char* RenderAntiAliasingModeLabel(RenderAntiAliasingMode mode) {
    return TEXT::EnumToName(kRenderAntiAliasingModeNames, mode, "Unknown");
}

const char* DlssQualityModeLabel(DlssQualityMode mode) {
    return TEXT::EnumToName(kDlssQualityModeLabels, mode, "Unknown");
}

const char* RenderFrameGenerationModeLabel(RenderFrameGenerationMode mode) {
    return TEXT::EnumToName(kRenderFrameGenerationModeLabels, mode, "Unknown");
}

const char* VolumetricLightingQualityLabel(VolumetricLightingQuality quality) {
    return TEXT::EnumToName(kVolumetricLightingQualityNames, quality, "Unknown");
}

const char* DynamicGiQualityLabel(DynamicGiQuality quality) {
    return TEXT::EnumToName(kDynamicGiQualityNames, quality, "Unknown");
}

const char* LocalShadowQualityLabel(LocalShadowQuality quality) {
    return TEXT::EnumToName(kLocalShadowQualityNames, quality, "Unknown");
}

const char* VolumetricCloudQualityLabel(VolumetricCloudQuality quality) {
    return TEXT::EnumToName(kVolumetricCloudQualityNames, quality, "Unknown");
}
const char* FluidSmokeQualityLabel(FluidSmokeQuality quality) {
    return TEXT::EnumToName(kFluidSmokeQualityNames, quality, "Unknown");
}

bool IsTemporalAntiAliasingMode(RenderAntiAliasingMode mode) { return mode == RenderAntiAliasingMode::TAA; }

bool UsesTemporalJitter(RenderAntiAliasingMode mode) {
    return mode == RenderAntiAliasingMode::TAA || mode == RenderAntiAliasingMode::DLAA ||
           mode == RenderAntiAliasingMode::DLSS;
}

bool IsDlaaAntiAliasingMode(RenderAntiAliasingMode mode) { return mode == RenderAntiAliasingMode::DLAA; }

bool IsDlssAntiAliasingMode(RenderAntiAliasingMode mode) { return mode == RenderAntiAliasingMode::DLSS; }

bool IsFxaaAntiAliasingMode(RenderAntiAliasingMode mode) { return mode == RenderAntiAliasingMode::FXAA; }

bool IsAntiAliasingModeAvailable(RenderAntiAliasingMode mode) {
    if (mode == RenderAntiAliasingMode::DLAA || mode == RenderAntiAliasingMode::DLSS) {
        return UPSCALING::IsStreamlineDlssAvailable();
    }
    return true;
}

bool IsFrameGenerationModeAvailable(RenderFrameGenerationMode mode) {
    return mode == RenderFrameGenerationMode::Off || UPSCALING::IsStreamlineFrameGenerationInstalled();
}

bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset) { return preset != RenderResolutionPreset::Viewport; }

RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset) {
    switch (preset) {
    case RenderResolutionPreset::P720: return {1280, 720};
    case RenderResolutionPreset::P1080: return {1920, 1080};
    case RenderResolutionPreset::P1440: return {2560, 1440};
    case RenderResolutionPreset::P2160: return {3840, 2160};
    case RenderResolutionPreset::Viewport:
    default: return {0, 0};
    }
}

RenderResolution ResolveSceneOutputResolution(const RenderQualitySettings& settings, int viewportWidth,
                                              int viewportHeight) {
    if (IsFixedRenderResolutionPreset(settings.sceneResolution)) {
        // 固定の预设は縦の画素数を決める。横は出す先（窓・编辑器の视口）の縦横比に合わせ、場面の投影と表示の縦横比を
        // 揃える（16:9 で描いた画を別の比率の视口へ伸ばすと、形が歪み、非等倍の拡縮で画がぼける）。
        const RenderResolution fixed = ResolveFixedRenderResolution(settings.sceneResolution);
        if (viewportWidth <= 0 || viewportHeight <= 0) {
            return fixed;
        }
        const double aspect = static_cast<double>(viewportWidth) / static_cast<double>(viewportHeight);
        return ClampResolution({static_cast<int>(static_cast<double>(fixed.height) * aspect + 0.5), fixed.height});
    }

    const float scale = std::clamp(settings.viewportScale, 0.25f, 2.0f);
    return ClampResolution({static_cast<int>(static_cast<float>((std::max)(viewportWidth, 1)) * scale + 0.5f),
                            static_cast<int>(static_cast<float>((std::max)(viewportHeight, 1)) * scale + 0.5f)});
}

} // namespace HIKARI::RENDER3D
