#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

#include <algorithm>

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
    }

    RenderQualitySettings& GetRenderQualitySettings() {
        return gRenderQualitySettings;
    }

    RenderQualitySettings NormalizeRenderQualitySettings(
        RenderQualitySettings settings) {
        settings.viewportScale = std::clamp(settings.viewportScale, 0.25f, 2.0f);
        settings.taaHistoryWeight =
            std::clamp(settings.taaHistoryWeight, 0.0f, 0.97f);
        settings.taaVarianceClipGamma =
            std::clamp(settings.taaVarianceClipGamma, 0.0f, 4.0f);
        settings.taaDepthRejection =
            std::clamp(settings.taaDepthRejection, 0.0001f, 0.05f);
        settings.taaLuminanceRejection =
            std::clamp(settings.taaLuminanceRejection, 0.05f, 4.0f);
        settings.taaSharpness = std::clamp(settings.taaSharpness, 0.0f, 1.0f);
        if (static_cast<uint8_t>(settings.sceneResolution) >
            static_cast<uint8_t>(RenderResolutionPreset::P2160)) {
            settings.sceneResolution = RenderResolutionPreset::P1080;
        }
        if (static_cast<uint8_t>(settings.windowSize) >
            static_cast<uint8_t>(RenderResolutionPreset::P2160) ||
            settings.windowSize == RenderResolutionPreset::Viewport) {
            settings.windowSize = RenderResolutionPreset::P720;
        }
        if (static_cast<uint8_t>(settings.windowMode) >
            static_cast<uint8_t>(WindowPresentationMode::Fullscreen)) {
            settings.windowMode = WindowPresentationMode::Windowed;
        }
        if (static_cast<uint8_t>(settings.geometryPipeline) >
            static_cast<uint8_t>(GeometryPipelineMode::AutoFallback)) {
            settings.geometryPipeline = GeometryPipelineMode::MeshShader;
        }
        if (static_cast<uint8_t>(settings.antiAliasingMode) >
            static_cast<uint8_t>(RenderAntiAliasingMode::DLSS)) {
            settings.antiAliasingMode = RenderAntiAliasingMode::Off;
        }
        if (static_cast<uint8_t>(settings.dlssQualityMode) >
            static_cast<uint8_t>(DlssQualityMode::UltraPerformance)) {
            settings.dlssQualityMode = DlssQualityMode::Quality;
        }
        if (static_cast<uint8_t>(settings.frameGenerationMode) >
            static_cast<uint8_t>(RenderFrameGenerationMode::Dlss)) {
            settings.frameGenerationMode = RenderFrameGenerationMode::Off;
        }
        settings.frameGenerationMultiplier =
            static_cast<uint8_t>(std::clamp(
                static_cast<int>(settings.frameGenerationMultiplier),
                2,
                6));
        if (static_cast<uint8_t>(settings.volumetricLightingQuality) >
            static_cast<uint8_t>(VolumetricLightingQuality::High)) {
            settings.volumetricLightingQuality = VolumetricLightingQuality::Balanced;
        }
        if (static_cast<uint8_t>(settings.forwardCostMode) >
            static_cast<uint8_t>(ForwardShadingCostMode::NoMaterialExtras)) {
            settings.forwardCostMode = ForwardShadingCostMode::Full;
        }
        if (static_cast<uint8_t>(settings.lightProbeVolumeSampling) >
            static_cast<uint8_t>(LightProbeVolumeSamplingMode::Off)) {
            settings.lightProbeVolumeSampling =
                LightProbeVolumeSamplingMode::FastSmooth;
        }
        return settings;
    }

    bool AreRenderQualitySettingsEqual(
        const RenderQualitySettings& lhs,
        const RenderQualitySettings& rhs) {
        return lhs.sceneResolution == rhs.sceneResolution &&
            lhs.windowSize == rhs.windowSize &&
            lhs.windowMode == rhs.windowMode &&
            lhs.geometryPipeline == rhs.geometryPipeline &&
            lhs.forwardCostMode == rhs.forwardCostMode &&
            lhs.lightProbeVolumeSampling == rhs.lightProbeVolumeSampling &&
            lhs.viewportScale == rhs.viewportScale &&
            lhs.vSync == rhs.vSync &&
            lhs.antiAliasingMode == rhs.antiAliasingMode &&
            lhs.dlssQualityMode == rhs.dlssQualityMode &&
            lhs.frameGenerationMode == rhs.frameGenerationMode &&
            lhs.frameGenerationMultiplier == rhs.frameGenerationMultiplier &&
            lhs.volumetricLightingQuality == rhs.volumetricLightingQuality &&
            lhs.taaHistoryWeight == rhs.taaHistoryWeight &&
            lhs.taaVarianceClipGamma == rhs.taaVarianceClipGamma &&
            lhs.taaDepthRejection == rhs.taaDepthRejection &&
            lhs.taaLuminanceRejection == rhs.taaLuminanceRejection &&
            lhs.taaSharpness == rhs.taaSharpness &&
            lhs.sceneDepthPrepass == rhs.sceneDepthPrepass;
    }

    void SetRenderQualitySettings(const RenderQualitySettings& settings) {
        gRenderQualitySettings = NormalizeRenderQualitySettings(settings);
    }

    const char* RenderResolutionPresetLabel(RenderResolutionPreset preset) {
        switch (preset) {
        case RenderResolutionPreset::Viewport: return "Viewport";
        case RenderResolutionPreset::P720: return "1280 x 720";
        case RenderResolutionPreset::P1080: return "1920 x 1080";
        case RenderResolutionPreset::P1440: return "2560 x 1440";
        case RenderResolutionPreset::P2160: return "3840 x 2160";
        default: return "Unknown";
        }
    }

    const char* WindowPresentationModeLabel(WindowPresentationMode mode) {
        switch (mode) {
        case WindowPresentationMode::Windowed: return "Windowed";
        case WindowPresentationMode::BorderlessWindow: return "Borderless";
        case WindowPresentationMode::Fullscreen: return "Fullscreen";
        default: return "Unknown";
        }
    }

    const char* GeometryPipelineModeLabel(GeometryPipelineMode mode) {
        switch (mode) {
        case GeometryPipelineMode::MeshShader: return "Mesh Shader";
        case GeometryPipelineMode::TraditionalVsPs: return "VS + PS";
        case GeometryPipelineMode::AutoFallback: return "Auto Fallback";
        default: return "Unknown";
        }
    }

    const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode) {
        switch (mode) {
        case ForwardShadingCostMode::Full: return "Full";
        case ForwardShadingCostMode::AlbedoOnly: return "Albedo Only";
        case ForwardShadingCostMode::NoNormalMap: return "No Normal Map";
        case ForwardShadingCostMode::NoShadow: return "No Shadow";
        case ForwardShadingCostMode::NoSsao: return "No SSAO";
        case ForwardShadingCostMode::NoMaterialExtras: return "No Material Extras";
        default: return "Unknown";
        }
    }

    const char* LightProbeVolumeSamplingModeLabel(LightProbeVolumeSamplingMode mode) {
        switch (mode) {
        case LightProbeVolumeSamplingMode::FastSmooth: return "Fast Smooth";
        case LightProbeVolumeSamplingMode::FullTrilinear: return "Full Trilinear";
        case LightProbeVolumeSamplingMode::Off: return "Off";
        default: return "Unknown";
        }
    }

    const char* RenderAntiAliasingModeLabel(RenderAntiAliasingMode mode) {
        switch (mode) {
        case RenderAntiAliasingMode::Off: return "Off";
        case RenderAntiAliasingMode::FXAA: return "FXAA";
        case RenderAntiAliasingMode::TAA: return "TAA";
        case RenderAntiAliasingMode::DLAA: return "DLAA";
        case RenderAntiAliasingMode::DLSS: return "DLSS";
        default: return "Unknown";
        }
    }

    const char* DlssQualityModeLabel(DlssQualityMode mode) {
        switch (mode) {
        case DlssQualityMode::Quality: return "Quality";
        case DlssQualityMode::Balanced: return "Balanced";
        case DlssQualityMode::Performance: return "Performance";
        case DlssQualityMode::UltraPerformance: return "Ultra Performance";
        default: return "Unknown";
        }
    }

    const char* RenderFrameGenerationModeLabel(
        RenderFrameGenerationMode mode) {
        switch (mode) {
        case RenderFrameGenerationMode::Off: return "Off";
        case RenderFrameGenerationMode::Dlss: return "DLSS Frame Generation";
        default: return "Unknown";
        }
    }

    const char* VolumetricLightingQualityLabel(VolumetricLightingQuality quality) {
        switch (quality) {
        case VolumetricLightingQuality::Low: return "Low";
        case VolumetricLightingQuality::Balanced: return "Balanced";
        case VolumetricLightingQuality::High: return "High";
        default: return "Unknown";
        }
    }

    bool IsTemporalAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::TAA;
    }

    bool UsesTemporalJitter(RenderAntiAliasingMode mode) {
        return
            mode == RenderAntiAliasingMode::TAA ||
            mode == RenderAntiAliasingMode::DLAA ||
            mode == RenderAntiAliasingMode::DLSS;
    }

    bool IsDlaaAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::DLAA;
    }

    bool IsDlssAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::DLSS;
    }

    bool IsFxaaAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::FXAA;
    }

    bool IsAntiAliasingModeAvailable(RenderAntiAliasingMode mode) {
        if (mode == RenderAntiAliasingMode::DLAA ||
            mode == RenderAntiAliasingMode::DLSS) {
            return UPSCALING::IsStreamlineDlssAvailable();
        }
        return true;
    }

    bool IsFrameGenerationModeAvailable(RenderFrameGenerationMode mode) {
        return mode == RenderFrameGenerationMode::Off ||
            UPSCALING::IsStreamlineFrameGenerationInstalled();
    }

    bool TryParseRenderAntiAliasingMode(
        std::string_view name,
        RenderAntiAliasingMode& outMode) {
        if (name == "Off" || name == "off") outMode = RenderAntiAliasingMode::Off;
        else if (name == "FXAA" || name == "fxaa") outMode = RenderAntiAliasingMode::FXAA;
        else if (name == "TAA" || name == "taa") outMode = RenderAntiAliasingMode::TAA;
        else if (name == "DLAA" || name == "dlaa") outMode = RenderAntiAliasingMode::DLAA;
        else if (name == "DLSS" || name == "dlss") outMode = RenderAntiAliasingMode::DLSS;
        else return false;
        return true;
    }

    bool TryParseDlssQualityMode(
        std::string_view name,
        DlssQualityMode& outMode) {
        if (name == "Quality" || name == "quality") outMode = DlssQualityMode::Quality;
        else if (name == "Balanced" || name == "balanced") outMode = DlssQualityMode::Balanced;
        else if (name == "Performance" || name == "performance") outMode = DlssQualityMode::Performance;
        else if (name == "Ultra Performance" || name == "UltraPerformance" ||
            name == "ultraPerformance" || name == "ultra_performance") {
            outMode = DlssQualityMode::UltraPerformance;
        }
        else return false;
        return true;
    }

    bool TryParseRenderFrameGenerationMode(
        std::string_view name,
        RenderFrameGenerationMode& outMode) {
        if (name == "Off" || name == "off") {
            outMode = RenderFrameGenerationMode::Off;
        }
        else if (name == "DLSS" || name == "dlss" ||
            name == "DLSS Frame Generation" || name == "Dlss") {
            outMode = RenderFrameGenerationMode::Dlss;
        }
        else {
            return false;
        }
        return true;
    }

    bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset) {
        return preset != RenderResolutionPreset::Viewport;
    }

    RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset) {
        switch (preset) {
        case RenderResolutionPreset::P720: return { 1280, 720 };
        case RenderResolutionPreset::P1080: return { 1920, 1080 };
        case RenderResolutionPreset::P1440: return { 2560, 1440 };
        case RenderResolutionPreset::P2160: return { 3840, 2160 };
        case RenderResolutionPreset::Viewport:
        default: return { 0, 0 };
        }
    }

    RenderResolution ResolveSceneOutputResolution(
        const RenderQualitySettings& settings,
        int viewportWidth,
        int viewportHeight) {

        if (IsFixedRenderResolutionPreset(settings.sceneResolution)) {
            return ResolveFixedRenderResolution(settings.sceneResolution);
        }

        const float scale = std::clamp(settings.viewportScale, 0.25f, 2.0f);
        return ClampResolution({
            static_cast<int>(static_cast<float>((std::max)(viewportWidth, 1)) * scale + 0.5f),
            static_cast<int>(static_cast<float>((std::max)(viewportHeight, 1)) * scale + 0.5f)
        });
    }

} // namespace HIKARI::RENDER3D
