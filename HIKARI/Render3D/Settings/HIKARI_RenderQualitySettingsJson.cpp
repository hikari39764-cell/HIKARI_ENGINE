#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"

namespace HIKARI::RENDER3D {

namespace {
template <typename Value>
bool ReadValue(const nlohmann::json& node, const char* key, Value& value) {
    if (!node.contains(key)) {
        return true;
    }
    try {
        value = node[key].get<Value>();
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadDirectionalShadow(const nlohmann::json& node, DirectionalShadowQualitySettings& settings) {
    const auto member = node.find("directionalShadow");
    if (member == node.end()) {
        return true;
    }
    if (!member->is_object()) {
        return false;
    }
    JSONFIELDS::ReadLenient(*member, settings, kDirectionalShadowFields);
    return true;
}

nlohmann::json WriteDirectionalShadow(const DirectionalShadowQualitySettings& settings) {
    nlohmann::json node = nlohmann::json::object();
    JSONFIELDS::Write(node, settings, kDirectionalShadowFields);
    return node;
}

// TEXT::EnumName の表で名前を読む。無ければ今の値のまま、知らない名前なら失敗。
template <class Enum, size_t Size>
bool ReadNamedEnum(const nlohmann::json& node, const char* key, const TEXT::EnumName<Enum> (&names)[Size],
                   Enum& value) {
    if (!node.contains(key)) {
        return true;
    }
    const nlohmann::json& name = node[key];
    return name.is_string() && TEXT::TryParseEnum(names, name.get<std::string>(), value);
}
} // namespace

nlohmann::json SerializeRenderQualitySettings(const RenderQualitySettings& source) {
    const RenderQualitySettings settings = NormalizeRenderQualitySettings(source);
    return {
        {"sceneResolution", TEXT::EnumToName(kRenderResolutionPresetNames, settings.sceneResolution)},
        {"windowSize", TEXT::EnumToName(kRenderResolutionPresetNames, settings.windowSize)},
        {"windowMode", TEXT::EnumToName(kWindowPresentationModeNames, settings.windowMode)},
        {"geometryPipeline", TEXT::EnumToName(kGeometryPipelineModeNames, settings.geometryPipeline)},
        {"forwardCostMode", TEXT::EnumToName(kForwardShadingCostModeNames, settings.forwardCostMode)},
        {"lightProbeVolumeSampling",
         TEXT::EnumToName(kLightProbeVolumeSamplingModeNames, settings.lightProbeVolumeSampling)},
        {"viewportScale", settings.viewportScale},
        {"vSync", settings.vSync},
        {"antiAliasingMode", TEXT::EnumToName(kRenderAntiAliasingModeNames, settings.antiAliasingMode)},
        {"dlssQualityMode", TEXT::EnumToName(kDlssQualityModeNames, settings.dlssQualityMode)},
        {"frameGenerationMode", TEXT::EnumToName(kRenderFrameGenerationModeNames, settings.frameGenerationMode)},
        {"frameGenerationMultiplier", settings.frameGenerationMultiplier},
        {"volumetricLightingQuality",
         TEXT::EnumToName(kVolumetricLightingQualityNames, settings.volumetricLightingQuality)},
        {"depthOfFieldQuality", TEXT::EnumToName(kDepthOfFieldQualityNames, settings.depthOfFieldQuality)},
        {"motionBlurQuality", TEXT::EnumToName(kMotionBlurQualityNames, settings.motionBlurQuality)},
        {"volumetricCloudQuality", TEXT::EnumToName(kVolumetricCloudQualityNames, settings.volumetricCloudQuality)},
        {"dynamicGiQuality", TEXT::EnumToName(kDynamicGiQualityNames, settings.dynamicGiQuality)},
        {"localShadowQuality", TEXT::EnumToName(kLocalShadowQualityNames, settings.localShadowQuality)},
        {"directionalShadow", WriteDirectionalShadow(settings.directionalShadow)},
        {"taaHistoryWeight", settings.taaHistoryWeight},
        {"taaVarianceClipGamma", settings.taaVarianceClipGamma},
        {"taaDepthRejection", settings.taaDepthRejection},
        {"taaLuminanceRejection", settings.taaLuminanceRejection},
        {"taaSharpness", settings.taaSharpness},
        {"sceneDepthPrepass", settings.sceneDepthPrepass},
        {"rayTracedReflections", settings.rayTracedReflections},
    };
}

bool DeserializeRenderQualitySettings(const nlohmann::json& node, RenderQualitySettings& inOutSettings,
                                      std::string* errorMessage) {
    if (!node.is_object()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Render quality settings must be a JSON object.";
        }
        return false;
    }

    RenderQualitySettings parsed = inOutSettings;
    const bool valid =
        ReadNamedEnum(node, "sceneResolution", kRenderResolutionPresetNames, parsed.sceneResolution) &&
        ReadNamedEnum(node, "windowSize", kRenderResolutionPresetNames, parsed.windowSize) &&
        ReadNamedEnum(node, "windowMode", kWindowPresentationModeNames, parsed.windowMode) &&
        ReadNamedEnum(node, "geometryPipeline", kGeometryPipelineModeNames, parsed.geometryPipeline) &&
        ReadNamedEnum(node, "forwardCostMode", kForwardShadingCostModeNames, parsed.forwardCostMode) &&
        ReadNamedEnum(node, "lightProbeVolumeSampling", kLightProbeVolumeSamplingModeNames,
                      parsed.lightProbeVolumeSampling) &&
        ReadValue(node, "viewportScale", parsed.viewportScale) && ReadValue(node, "vSync", parsed.vSync) &&
        ReadNamedEnum(node, "antiAliasingMode", kRenderAntiAliasingModeNames, parsed.antiAliasingMode) &&
        ReadNamedEnum(node, "dlssQualityMode", kDlssQualityModeNames, parsed.dlssQualityMode) &&
        ReadNamedEnum(node, "frameGenerationMode", kRenderFrameGenerationModeNames, parsed.frameGenerationMode) &&
        ReadValue(node, "frameGenerationMultiplier", parsed.frameGenerationMultiplier) &&
        ReadNamedEnum(node, "volumetricLightingQuality", kVolumetricLightingQualityNames,
                      parsed.volumetricLightingQuality) &&
        ReadNamedEnum(node, "depthOfFieldQuality", kDepthOfFieldQualityNames, parsed.depthOfFieldQuality) &&
        ReadNamedEnum(node, "motionBlurQuality", kMotionBlurQualityNames, parsed.motionBlurQuality) &&
        ReadNamedEnum(node, "volumetricCloudQuality", kVolumetricCloudQualityNames, parsed.volumetricCloudQuality) &&
        ReadNamedEnum(node, "dynamicGiQuality", kDynamicGiQualityNames, parsed.dynamicGiQuality) &&
        ReadNamedEnum(node, "localShadowQuality", kLocalShadowQualityNames, parsed.localShadowQuality) &&
        ReadDirectionalShadow(node, parsed.directionalShadow) &&
        ReadValue(node, "taaHistoryWeight", parsed.taaHistoryWeight) &&
        ReadValue(node, "taaVarianceClipGamma", parsed.taaVarianceClipGamma) &&
        ReadValue(node, "taaDepthRejection", parsed.taaDepthRejection) &&
        ReadValue(node, "taaLuminanceRejection", parsed.taaLuminanceRejection) &&
        ReadValue(node, "taaSharpness", parsed.taaSharpness) &&
        ReadValue(node, "sceneDepthPrepass", parsed.sceneDepthPrepass) &&
        ReadValue(node, "rayTracedReflections", parsed.rayTracedReflections);
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
