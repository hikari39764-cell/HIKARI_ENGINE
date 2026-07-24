#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

#include <array>
#include <string_view>

namespace HIKARI::RENDER3D {

    namespace {
        template <typename Enum, size_t Size>
        using EnumNames = std::array<std::pair<Enum, std::string_view>, Size>;

        constexpr EnumNames<RenderResolutionPreset, 5> kResolutionNames{ {
            { RenderResolutionPreset::Viewport, "Viewport" },
            { RenderResolutionPreset::P720, "720p" },
            { RenderResolutionPreset::P1080, "1080p" },
            { RenderResolutionPreset::P1440, "1440p" },
            { RenderResolutionPreset::P2160, "2160p" },
        } };
        constexpr EnumNames<WindowPresentationMode, 3> kWindowModeNames{ {
            { WindowPresentationMode::Windowed, "Windowed" },
            { WindowPresentationMode::BorderlessWindow, "Borderless" },
            { WindowPresentationMode::Fullscreen, "Fullscreen" },
        } };
        constexpr EnumNames<GeometryPipelineMode, 3> kGeometryNames{ {
            { GeometryPipelineMode::MeshShader, "MeshShader" },
            { GeometryPipelineMode::TraditionalVsPs, "TraditionalVsPs" },
            { GeometryPipelineMode::AutoFallback, "AutoFallback" },
        } };
        constexpr EnumNames<ForwardShadingCostMode, 6> kForwardCostNames{ {
            { ForwardShadingCostMode::Full, "Full" },
            { ForwardShadingCostMode::AlbedoOnly, "AlbedoOnly" },
            { ForwardShadingCostMode::NoNormalMap, "NoNormalMap" },
            { ForwardShadingCostMode::NoShadow, "NoShadow" },
            { ForwardShadingCostMode::NoSsao, "NoSsao" },
            { ForwardShadingCostMode::NoMaterialExtras, "NoMaterialExtras" },
        } };
        constexpr EnumNames<LightProbeVolumeSamplingMode, 3> kLightProbeNames{ {
            { LightProbeVolumeSamplingMode::FastSmooth, "FastSmooth" },
            { LightProbeVolumeSamplingMode::FullTrilinear, "FullTrilinear" },
            { LightProbeVolumeSamplingMode::Off, "Off" },
        } };
        constexpr EnumNames<RenderAntiAliasingMode, 5> kAntiAliasingNames{ {
            { RenderAntiAliasingMode::Off, "Off" },
            { RenderAntiAliasingMode::FXAA, "FXAA" },
            { RenderAntiAliasingMode::TAA, "TAA" },
            { RenderAntiAliasingMode::DLAA, "DLAA" },
            { RenderAntiAliasingMode::DLSS, "DLSS" },
        } };
        constexpr EnumNames<DlssQualityMode, 4> kDlssQualityNames{ {
            { DlssQualityMode::Quality, "Quality" },
            { DlssQualityMode::Balanced, "Balanced" },
            { DlssQualityMode::Performance, "Performance" },
            { DlssQualityMode::UltraPerformance, "UltraPerformance" },
        } };
        constexpr EnumNames<RenderFrameGenerationMode, 2> kFrameGenerationNames{ {
            { RenderFrameGenerationMode::Off, "Off" },
            { RenderFrameGenerationMode::Dlss, "DLSS" },
        } };
        constexpr EnumNames<VolumetricLightingQuality, 3> kVolumetricNames{ {
            { VolumetricLightingQuality::Low, "Low" },
            { VolumetricLightingQuality::Balanced, "Balanced" },
            { VolumetricLightingQuality::High, "High" },
        } };

        template <typename Enum, size_t Size>
        std::string_view EnumName(Enum value, const EnumNames<Enum, Size>& names) {
            for (const auto& [candidate, name] : names) {
                if (candidate == value) {
                    return name;
                }
            }
            return names.front().second;
        }

        template <typename Enum, size_t Size>
        bool ReadEnum(
            const nlohmann::json& node,
            const char* key,
            const EnumNames<Enum, Size>& names,
            Enum& value) {
            if (!node.contains(key)) {
                return true;
            }
            if (!node[key].is_string()) {
                return false;
            }
            const std::string name = node[key].get<std::string>();
            for (const auto& [candidate, candidateName] : names) {
                if (name == candidateName) {
                    value = candidate;
                    return true;
                }
            }
            return false;
        }

        template <typename Value>
        bool ReadValue(const nlohmann::json& node, const char* key, Value& value) {
            if (!node.contains(key)) {
                return true;
            }
            try {
                value = node[key].get<Value>();
                return true;
            }
            catch (...) {
                return false;
            }
        }
    }

    nlohmann::json SerializeRenderQualitySettings(
        const RenderQualitySettings& source) {
        const RenderQualitySettings settings =
            NormalizeRenderQualitySettings(source);
        return {
            { "sceneResolution", EnumName(settings.sceneResolution, kResolutionNames) },
            { "windowSize", EnumName(settings.windowSize, kResolutionNames) },
            { "windowMode", EnumName(settings.windowMode, kWindowModeNames) },
            { "geometryPipeline", EnumName(settings.geometryPipeline, kGeometryNames) },
            { "forwardCostMode", EnumName(settings.forwardCostMode, kForwardCostNames) },
            { "lightProbeVolumeSampling", EnumName(settings.lightProbeVolumeSampling, kLightProbeNames) },
            { "viewportScale", settings.viewportScale },
            { "vSync", settings.vSync },
            { "antiAliasingMode", EnumName(settings.antiAliasingMode, kAntiAliasingNames) },
            { "dlssQualityMode", EnumName(settings.dlssQualityMode, kDlssQualityNames) },
            { "frameGenerationMode", EnumName(settings.frameGenerationMode, kFrameGenerationNames) },
            { "frameGenerationMultiplier", settings.frameGenerationMultiplier },
            { "volumetricLightingQuality", EnumName(settings.volumetricLightingQuality, kVolumetricNames) },
            { "taaHistoryWeight", settings.taaHistoryWeight },
            { "taaVarianceClipGamma", settings.taaVarianceClipGamma },
            { "taaDepthRejection", settings.taaDepthRejection },
            { "taaLuminanceRejection", settings.taaLuminanceRejection },
            { "taaSharpness", settings.taaSharpness },
            { "sceneDepthPrepass", settings.sceneDepthPrepass },
        };
    }

    bool DeserializeRenderQualitySettings(
        const nlohmann::json& node,
        RenderQualitySettings& inOutSettings,
        std::string* errorMessage) {
        if (!node.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Render quality settings must be a JSON object.";
            }
            return false;
        }

        RenderQualitySettings parsed = inOutSettings;
        const bool valid =
            ReadEnum(node, "sceneResolution", kResolutionNames, parsed.sceneResolution) &&
            ReadEnum(node, "windowSize", kResolutionNames, parsed.windowSize) &&
            ReadEnum(node, "windowMode", kWindowModeNames, parsed.windowMode) &&
            ReadEnum(node, "geometryPipeline", kGeometryNames, parsed.geometryPipeline) &&
            ReadEnum(node, "forwardCostMode", kForwardCostNames, parsed.forwardCostMode) &&
            ReadEnum(node, "lightProbeVolumeSampling", kLightProbeNames, parsed.lightProbeVolumeSampling) &&
            ReadValue(node, "viewportScale", parsed.viewportScale) &&
            ReadValue(node, "vSync", parsed.vSync) &&
            ReadEnum(node, "antiAliasingMode", kAntiAliasingNames, parsed.antiAliasingMode) &&
            ReadEnum(node, "dlssQualityMode", kDlssQualityNames, parsed.dlssQualityMode) &&
            ReadEnum(node, "frameGenerationMode", kFrameGenerationNames, parsed.frameGenerationMode) &&
            ReadValue(node, "frameGenerationMultiplier", parsed.frameGenerationMultiplier) &&
            ReadEnum(node, "volumetricLightingQuality", kVolumetricNames, parsed.volumetricLightingQuality) &&
            ReadValue(node, "taaHistoryWeight", parsed.taaHistoryWeight) &&
            ReadValue(node, "taaVarianceClipGamma", parsed.taaVarianceClipGamma) &&
            ReadValue(node, "taaDepthRejection", parsed.taaDepthRejection) &&
            ReadValue(node, "taaLuminanceRejection", parsed.taaLuminanceRejection) &&
            ReadValue(node, "taaSharpness", parsed.taaSharpness) &&
            ReadValue(node, "sceneDepthPrepass", parsed.sceneDepthPrepass);
        if (!valid) {
            if (errorMessage != nullptr) {
                *errorMessage = "Render quality settings contain an invalid field.";
            }
            return false;
        }

        inOutSettings = NormalizeRenderQualitySettings(parsed);
        return true;
    }

} // namespace HIKARI::RENDER3D
