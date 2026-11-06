#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

// 画質設定の項目表。設定ファイルの読み書き・値の正規化・コンソール変数（r.）が共有する。画質パネルは太陽の影の節を
// この表で描き、他の項目の範囲はまだパネルに手書き（同じ値に揃えてある）。
namespace HIKARI::RENDER3D {

inline constexpr auto kDirectionalShadowFields = [](auto&& field) {
    using S = DirectionalShadowQualitySettings;
    using JSONFIELDS::Range;
    using JSONFIELDS::Slider;
    using JSONFIELDS::Unit;
    field("resolution", &S::resolution, Range{256.0, 8192.0}, Unit{"px"});
    field("cascadeCount", &S::cascadeCount, Slider(1.0, 4.0));
    field("cascadeSplitLambda", &S::cascadeSplitLambda, Slider(0.0, 1.0));
    field("cascadeBlend", &S::cascadeBlend, Slider(0.0, 0.3));
    field("nearPlane", &S::nearPlane, Range{0.01, 100.0}, Unit{"m"});
    field("farPlane", &S::farPlane, Range{1.0, 100000.0}, Unit{"m"});
    field("depthBias", &S::depthBias, Range{0.0, 0.1});
    field("normalBias", &S::normalBias, Range{0.0, 1.0});
    field("pcfEnabled", &S::pcfEnabled);
    field("pcfRadius", &S::pcfRadius, Range{0.0, 8.0});
    field("penumbraScale", &S::penumbraScale, Range{0.0, 16.0});
    field("rayTraced", &S::rayTraced);
    field("rayTracedSamples", &S::rayTracedSamples, Range{1, 8});
    field("stabilize", &S::stabilize);
};

// 画質設定の全項目。キーは render_quality.json の保存名と同じで、範囲は NormalizeRenderQualitySettings が収める範囲。
inline constexpr auto kRenderQualitySettingsFields = [](auto&& field) {
    using S = RenderQualitySettings;
    using JSONFIELDS::Nested;
    using JSONFIELDS::Range;
    using JSONFIELDS::Unit;
    field("sceneResolution", &S::sceneResolution, kRenderResolutionPresetNames);
    field("windowSize", &S::windowSize, kRenderResolutionPresetNames);
    field("windowMode", &S::windowMode, kWindowPresentationModeNames);
    field("geometryPipeline", &S::geometryPipeline, kGeometryPipelineModeNames);
    field("forwardCostMode", &S::forwardCostMode, kForwardShadingCostModeNames);
    field("lightProbeVolumeSampling", &S::lightProbeVolumeSampling, kLightProbeVolumeSamplingModeNames);
    field("viewportScale", &S::viewportScale, Range{0.25, 2.0}, Unit{"x"});
    field("vSync", &S::vSync);
    field("antiAliasingMode", &S::antiAliasingMode, kRenderAntiAliasingModeNames);
    field("dlssQualityMode", &S::dlssQualityMode, kDlssQualityModeNames);
    field("frameGenerationMode", &S::frameGenerationMode, kRenderFrameGenerationModeNames);
    field("frameGenerationMultiplier", &S::frameGenerationMultiplier, Range{2, 6}, Unit{"x"});
    field("volumetricLightingQuality", &S::volumetricLightingQuality, kVolumetricLightingQualityNames);
    field("depthOfFieldQuality", &S::depthOfFieldQuality, kDepthOfFieldQualityNames);
    field("motionBlurQuality", &S::motionBlurQuality, kMotionBlurQualityNames);
    field("volumetricCloudQuality", &S::volumetricCloudQuality, kVolumetricCloudQualityNames);
    field("fluidSmokeQuality", &S::fluidSmokeQuality, kFluidSmokeQualityNames);
    field("dynamicGiQuality", &S::dynamicGiQuality, kDynamicGiQualityNames);
    field("localShadowQuality", &S::localShadowQuality, kLocalShadowQualityNames);
    field("directionalShadow", &S::directionalShadow, Nested{kDirectionalShadowFields});
    field("taaHistoryWeight", &S::taaHistoryWeight, Range{0.0, 0.97});
    field("taaVarianceClipGamma", &S::taaVarianceClipGamma, Range{0.0, 4.0});
    field("taaDepthRejection", &S::taaDepthRejection, Range{0.0001, 0.05});
    field("taaLuminanceRejection", &S::taaLuminanceRejection, Range{0.05, 4.0});
    field("taaSharpness", &S::taaSharpness, Range{0.0, 1.0});
    field("sceneDepthPrepass", &S::sceneDepthPrepass);
    field("rayTracedReflections", &S::rayTracedReflections);
};

} // namespace HIKARI::RENDER3D
