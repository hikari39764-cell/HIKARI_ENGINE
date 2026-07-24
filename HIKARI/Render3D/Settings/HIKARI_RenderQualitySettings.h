#pragma once

#include <cstdint>
#include <string_view>

namespace HIKARI::RENDER3D {

    enum class RenderResolutionPreset : uint8_t {
        Viewport = 0,
        P720,
        P1080,
        P1440,
        P2160,
    };

    enum class WindowPresentationMode : uint8_t {
        Windowed = 0,
        BorderlessWindow,
        Fullscreen,
    };

    enum class GeometryPipelineMode : uint8_t {
        MeshShader = 0,
        TraditionalVsPs,
        AutoFallback,
    };

    enum class ForwardShadingCostMode : uint8_t {
        Full = 0,
        AlbedoOnly,
        NoNormalMap,
        NoShadow,
        NoSsao,
        NoMaterialExtras,
    };

    enum class LightProbeVolumeSamplingMode : uint8_t {
        FastSmooth = 0,
        FullTrilinear,
        Off,
    };

    enum class RenderAntiAliasingMode : uint8_t {
        Off = 0,
        FXAA,
        TAA,
        DLAA,
        DLSS,
    };

    enum class DlssQualityMode : uint8_t {
        Quality = 0,
        Balanced,
        Performance,
        UltraPerformance,
    };

    enum class RenderFrameGenerationMode : uint8_t {
        Off = 0,
        Dlss,
    };

    enum class VolumetricLightingQuality : uint8_t {
        Low = 0,
        Balanced,
        High,
    };

    struct RenderResolution {
        int width = 0;
        int height = 0;
    };

    struct RenderQualitySettings {
        RenderResolutionPreset sceneResolution = RenderResolutionPreset::P1080;
        RenderResolutionPreset windowSize = RenderResolutionPreset::P720;
        WindowPresentationMode windowMode = WindowPresentationMode::Windowed;
        GeometryPipelineMode geometryPipeline = GeometryPipelineMode::MeshShader;
        ForwardShadingCostMode forwardCostMode = ForwardShadingCostMode::Full;
        LightProbeVolumeSamplingMode lightProbeVolumeSampling =
            LightProbeVolumeSamplingMode::FastSmooth;
        float viewportScale = 1.0f;
        bool vSync = false;
        RenderAntiAliasingMode antiAliasingMode = RenderAntiAliasingMode::TAA;
        DlssQualityMode dlssQualityMode = DlssQualityMode::Quality;
        RenderFrameGenerationMode frameGenerationMode =
            RenderFrameGenerationMode::Off;
        uint8_t frameGenerationMultiplier = 2;
        VolumetricLightingQuality volumetricLightingQuality =
            VolumetricLightingQuality::Balanced;
        float taaHistoryWeight = 0.92f;
        float taaVarianceClipGamma = 1.25f;
        float taaDepthRejection = 0.0025f;
        float taaLuminanceRejection = 0.55f;
        float taaSharpness = 0.25f;
        // Optional scene-depth prepass for overdraw-heavy scenes.
        // Geometry-bound scenes should leave this off.
        bool sceneDepthPrepass = false;
    };

    RenderQualitySettings& GetRenderQualitySettings();
    RenderQualitySettings NormalizeRenderQualitySettings(
        RenderQualitySettings settings);
    bool AreRenderQualitySettingsEqual(
        const RenderQualitySettings& lhs,
        const RenderQualitySettings& rhs);
    void SetRenderQualitySettings(const RenderQualitySettings& settings);

    const char* RenderResolutionPresetLabel(RenderResolutionPreset preset);
    const char* WindowPresentationModeLabel(WindowPresentationMode mode);
    const char* GeometryPipelineModeLabel(GeometryPipelineMode mode);
    const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode);
    const char* LightProbeVolumeSamplingModeLabel(LightProbeVolumeSamplingMode mode);
    const char* RenderAntiAliasingModeLabel(RenderAntiAliasingMode mode);
    const char* DlssQualityModeLabel(DlssQualityMode mode);
    const char* RenderFrameGenerationModeLabel(RenderFrameGenerationMode mode);
    const char* VolumetricLightingQualityLabel(VolumetricLightingQuality quality);

    bool IsTemporalAntiAliasingMode(RenderAntiAliasingMode mode);
    bool UsesTemporalJitter(RenderAntiAliasingMode mode);
    bool IsDlaaAntiAliasingMode(RenderAntiAliasingMode mode);
    bool IsDlssAntiAliasingMode(RenderAntiAliasingMode mode);
    bool IsFxaaAntiAliasingMode(RenderAntiAliasingMode mode);
    bool IsAntiAliasingModeAvailable(RenderAntiAliasingMode mode);
    bool IsFrameGenerationModeAvailable(RenderFrameGenerationMode mode);

    bool TryParseRenderAntiAliasingMode(
        std::string_view name,
        RenderAntiAliasingMode& outMode);
    bool TryParseDlssQualityMode(
        std::string_view name,
        DlssQualityMode& outMode);
    bool TryParseRenderFrameGenerationMode(
        std::string_view name,
        RenderFrameGenerationMode& outMode);

    bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset);
    RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset);
    RenderResolution ResolveSceneOutputResolution(
        const RenderQualitySettings& settings,
        int viewportWidth,
        int viewportHeight);

} // namespace HIKARI::RENDER3D
