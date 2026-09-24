#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironmentNames.h"

namespace HIKARI::SCENE::SERIALIZATION {

namespace JsonMath = ::HIKARI::SERIALIZATION::JSON::MATH;

namespace {

DirectX::XMFLOAT4 DeserializeFloat4(const nlohmann::json& node, const DirectX::XMFLOAT4& fallback) {
    if (!node.is_array() || node.size() < 4) {
        return fallback;
    }
    return {node[0].is_number() ? node[0].get<float>() : fallback.x,
            node[1].is_number() ? node[1].get<float>() : fallback.y,
            node[2].is_number() ? node[2].get<float>() : fallback.z,
            node[3].is_number() ? node[3].get<float>() : fallback.w};
}

constexpr TEXT::EnumName<SkyMode> kSkyModeNames[] = {
    {SkyMode::None, "None"},           {SkyMode::Gradient, "Gradient"},     {SkyMode::Cubemap, "Cubemap"},
    {SkyMode::Texture2D, "Texture2D"}, {SkyMode::Atmosphere, "Atmosphere"}, {SkyMode::Atmosphere, "Atmospheric"},
    {SkyMode::Atmosphere, "Physical"},
};
constexpr TEXT::EnumName<RENDER3D::COLOR::ExposureMode> kExposureModeNames[] = {
    {RENDER3D::COLOR::ExposureMode::Manual, "Manual"},
    {RENDER3D::COLOR::ExposureMode::Automatic, "Automatic"},
    {RENDER3D::COLOR::ExposureMode::Manual, "manual"},
    {RENDER3D::COLOR::ExposureMode::Automatic, "automatic"},
};
constexpr TEXT::EnumName<ReflectionProbeInfluenceShape> kInfluenceShapeNames[] = {
    {ReflectionProbeInfluenceShape::Sphere, "Sphere"},
    {ReflectionProbeInfluenceShape::Box, "Box"},
};
constexpr TEXT::EnumName<ReflectionProbeProjectionShape> kProjectionShapeNames[] = {
    {ReflectionProbeProjectionShape::Infinite, "Infinite"},
    {ReflectionProbeProjectionShape::Box, "Box"},
};
constexpr TEXT::EnumName<ScreenSpaceReflectionQuality> kSsrQualityNames[] = {
    {ScreenSpaceReflectionQuality::Off, "Off"},
    {ScreenSpaceReflectionQuality::Low, "Low"},
    {ScreenSpaceReflectionQuality::Medium, "Medium"},
    {ScreenSpaceReflectionQuality::High, "High"},
};

const char* ToString(SkyMode mode) { return TEXT::EnumToName(kSkyModeNames, mode, "Gradient"); }
SkyMode ParseSkyMode(const nlohmann::json& node, SkyMode fallback) {
    return JSONREAD::EnumOr(node, kSkyModeNames, fallback, true);
}

const char* ExposureModeName(RENDER3D::COLOR::ExposureMode mode) {
    return TEXT::EnumToName(kExposureModeNames, mode, "Manual");
}
// 旧形式の整数は Automatic(1) 以外を Manual として読む
RENDER3D::COLOR::ExposureMode ParseExposureMode(const nlohmann::json& node, RENDER3D::COLOR::ExposureMode fallback) {
    if (node.is_number_integer()) {
        return node.get<int>() == static_cast<int>(RENDER3D::COLOR::ExposureMode::Automatic)
                   ? RENDER3D::COLOR::ExposureMode::Automatic
                   : RENDER3D::COLOR::ExposureMode::Manual;
    }
    return JSONREAD::EnumOr(node, kExposureModeNames, fallback);
}

const char* ToString(ReflectionProbeInfluenceShape shape) {
    return TEXT::EnumToName(kInfluenceShapeNames, shape, "Sphere");
}
const char* ToString(ReflectionProbeProjectionShape shape) {
    return TEXT::EnumToName(kProjectionShapeNames, shape, "Infinite");
}
// 旧形式の整数は 1 だけを Box として受け付ける
template <class Shape, std::size_t N>
Shape ParseProbeShape(const nlohmann::json& node, const TEXT::EnumName<Shape> (&names)[N], Shape fallback) {
    if (node.is_number_integer()) {
        return node.get<int>() == 1 ? Shape::Box : fallback;
    }
    return JSONREAD::EnumOr(node, names, fallback);
}

SsaoMode ParseSsaoMode(const nlohmann::json& node, SsaoMode fallback) {
    return JSONREAD::EnumOr(node, kSsaoModeNames, fallback, true);
}
const char* ToString(ScreenSpaceReflectionQuality quality) {
    return TEXT::EnumToName(kSsrQualityNames, quality, "Medium");
}
ScreenSpaceReflectionQuality ParseSsrQuality(const nlohmann::json& node, ScreenSpaceReflectionQuality fallback) {
    return JSONREAD::EnumOr(node, kSsrQualityNames, fallback, true);
}

MATH::Vec3 ReflectionProbeRadiusBoxSize(float radius) {
    const float diameter = std::max(0.01f, radius * 2.0f);
    return {diameter, diameter, diameter};
}

constexpr float kCloudColorMin = 0.0f;
constexpr float kCloudColorMax = 16.0f;
constexpr float kCloudDirectionMin = -10000.0f;
constexpr float kCloudDirectionMax = 10000.0f;

float SanitizeCloudFloat(float value, float fallback, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

float ReadCloudNumber(const nlohmann::json* value, float fallback, float minimum, float maximum) {
    if (value == nullptr || !value->is_number()) {
        return fallback;
    }
    try {
        const double parsed = value->get<double>();
        if (!std::isfinite(parsed)) {
            return fallback;
        }
        const double clamped = std::clamp(parsed, static_cast<double>(minimum), static_cast<double>(maximum));
        const float result = static_cast<float>(clamped);
        return std::isfinite(result) ? result : fallback;
    } catch (...) {
        return fallback;
    }
}

float ReadCloudMember(const nlohmann::json& object, const char* key, float fallback, float minimum, float maximum) {
    return ReadCloudNumber(JSONREAD::FindMember(object, key), fallback, minimum, maximum);
}

float ReadCloudArrayElement(const nlohmann::json& node, std::size_t index, float fallback, float minimum,
                            float maximum) {
    if (!node.is_array() || node.size() <= index) {
        return fallback;
    }
    return ReadCloudNumber(&node[index], fallback, minimum, maximum);
}

MATH::Vec2 ReadCloudVec2(const nlohmann::json& node, const MATH::Vec2& fallback, float minimum, float maximum) {
    MATH::Vec2 value = fallback;
    if (node.is_array()) {
        value.x = ReadCloudArrayElement(node, 0u, value.x, minimum, maximum);
        value.y = ReadCloudArrayElement(node, 1u, value.y, minimum, maximum);
    } else if (node.is_object()) {
        value.x = ReadCloudMember(node, "x", value.x, minimum, maximum);
        value.y = ReadCloudMember(node, "y", value.y, minimum, maximum);
    }
    return value;
}

MATH::Vec3 ReadCloudVec3(const nlohmann::json& node, const MATH::Vec3& fallback, float minimum, float maximum) {
    MATH::Vec3 value = fallback;
    if (node.is_array()) {
        value.x = ReadCloudArrayElement(node, 0u, value.x, minimum, maximum);
        value.y = ReadCloudArrayElement(node, 1u, value.y, minimum, maximum);
        value.z = ReadCloudArrayElement(node, 2u, value.z, minimum, maximum);
    } else if (node.is_object()) {
        value.x = ReadCloudMember(node, "x", value.x, minimum, maximum);
        value.y = ReadCloudMember(node, "y", value.y, minimum, maximum);
        value.z = ReadCloudMember(node, "z", value.z, minimum, maximum);
    }
    return value;
}

MATH::Vec2 SanitizeCloudVec2(const MATH::Vec2& value, const MATH::Vec2& fallback, float minimum,
                             float maximum) noexcept {
    return {
        SanitizeCloudFloat(value.x, fallback.x, minimum, maximum),
        SanitizeCloudFloat(value.y, fallback.y, minimum, maximum),
    };
}

MATH::Vec3 SanitizeCloudVec3(const MATH::Vec3& value, const MATH::Vec3& fallback, float minimum,
                             float maximum) noexcept {
    return {
        SanitizeCloudFloat(value.x, fallback.x, minimum, maximum),
        SanitizeCloudFloat(value.y, fallback.y, minimum, maximum),
        SanitizeCloudFloat(value.z, fallback.z, minimum, maximum),
    };
}

float ReadAtmosphereMember(const nlohmann::json& object, const char* key, float fallback, float minimum,
                           float maximum) {
    return ReadCloudMember(object, key, fallback, minimum, maximum);
}

MATH::Vec3 ReadAtmosphereVec3(const nlohmann::json& node, const MATH::Vec3& fallback, float minimum, float maximum) {
    return ReadCloudVec3(node, fallback, minimum, maximum);
}

uint32_t ReadAtmosphereUInt(const nlohmann::json& object, const char* key, uint32_t fallback, uint32_t minimum,
                            uint32_t maximum) {
    const float parsed = ReadAtmosphereMember(object, key, static_cast<float>(fallback), static_cast<float>(minimum),
                                              static_cast<float>(maximum));
    if (!std::isfinite(parsed)) {
        return fallback;
    }
    return static_cast<uint32_t>(parsed);
}

template <class Settings>
struct EnvironmentFloatField {
    const char* name;
    float Settings::* member;
    float minimum, maximum;
};
template <class Settings, size_t Count>
void WriteEnvironmentFloats(nlohmann::json& node, const Settings& settings,
                            const EnvironmentFloatField<Settings> (&fields)[Count]) {
    const Settings defaults{};
    for (const auto& field : fields)
        node[field.name] =
            SanitizeCloudFloat(settings.*field.member, defaults.*field.member, field.minimum, field.maximum);
}
template <class Settings, size_t Count>
void ReadEnvironmentFloats(const nlohmann::json& node, Settings& settings,
                           const EnvironmentFloatField<Settings> (&fields)[Count]) {
    const Settings defaults{};
    for (const auto& field : fields)
        settings.*field.member =
            ReadCloudMember(node, field.name, defaults.*field.member, field.minimum, field.maximum);
}
constexpr EnvironmentFloatField<AtmosphereSettings> kAtmosphereSettingsFields[]{
    {"planetRadius", &AtmosphereSettings::planetRadius, 1.0f, 100000000.0f},
    {"atmosphereHeight", &AtmosphereSettings::atmosphereHeight, 1.0f, 1000000.0f},
    {"rayleighScaleHeight", &AtmosphereSettings::rayleighScaleHeight, 1.0f, 1000000.0f},
    {"mieScaleHeight", &AtmosphereSettings::mieScaleHeight, 1.0f, 1000000.0f},
    {"ozoneCenterHeight", &AtmosphereSettings::ozoneCenterHeight, 0.0f, 1000000.0f},
    {"ozoneWidth", &AtmosphereSettings::ozoneWidth, 1.0f, 1000000.0f},
    {"mieAnisotropy", &AtmosphereSettings::mieAnisotropy, -0.95f, 0.95f},
    {"sunIlluminance", &AtmosphereSettings::sunIlluminance, 0.0f, 16.0f},
    {"sunExposure", &AtmosphereSettings::sunExposure, -16.0f, 16.0f},
    {"groundAlbedo", &AtmosphereSettings::groundAlbedo, 0.0f, 1.0f},
    {"aerialPerspectiveStrength", &AtmosphereSettings::aerialPerspectiveStrength, 0.0f, 8.0f},
    {"sunAngularRadius", &AtmosphereSettings::sunAngularRadius, 0.0001f, 0.25f},
};

constexpr EnvironmentFloatField<SunGlareSettings> kSunGlareSettingsFields[]{
    {"intensity", &SunGlareSettings::intensity, 0.0f, 8.0f},
    {"haloRadius", &SunGlareSettings::haloRadius, 0.0f, 2.0f},
    {"haloFalloff", &SunGlareSettings::haloFalloff, 0.1f, 16.0f},
    {"streakIntensity", &SunGlareSettings::streakIntensity, 0.0f, 8.0f},
    {"streakLength", &SunGlareSettings::streakLength, 0.0f, 2.0f},
    {"occlusionRadius", &SunGlareSettings::occlusionRadius, 0.0f, 0.25f},
    {"edgeFade", &SunGlareSettings::edgeFade, 0.0f, 1.0f},
    {"maxRadiance", &SunGlareSettings::maxRadiance, 0.0f, 64.0f},
    {"shaftIntensity", &SunGlareSettings::shaftIntensity, 0.0f, 8.0f},
    {"shaftLength", &SunGlareSettings::shaftLength, 0.01f, 2.0f},
    {"shaftDecay", &SunGlareSettings::shaftDecay, 0.5f, 0.999f},
};

constexpr EnvironmentFloatField<StylizedCloudSettings> kStylizedCloudSettingsFields[]{
    {"coverage", &StylizedCloudSettings::coverage, 0.0f, 1.0f},
    {"density", &StylizedCloudSettings::density, 0.0f, 8.0f},
    {"altitude", &StylizedCloudSettings::altitude, 0.0f, 100000.0f},
    {"thickness", &StylizedCloudSettings::thickness, 0.1f, 100000.0f},
    {"scale", &StylizedCloudSettings::scale, 0.1f, 100000.0f},
    {"windSpeed", &StylizedCloudSettings::windSpeed, 0.0f, 1000.0f},
    {"shapeDetail", &StylizedCloudSettings::shapeDetail, 0.0f, 1.0f},
    {"softness", &StylizedCloudSettings::softness, 0.0f, 1.0f},
    {"anisotropy", &StylizedCloudSettings::anisotropy, -0.8f, 0.8f},
    {"lightAbsorption", &StylizedCloudSettings::lightAbsorption, 0.0f, 8.0f},
    {"silverLining", &StylizedCloudSettings::silverLining, 0.0f, 4.0f},
    {"horizonFade", &StylizedCloudSettings::horizonFade, 0.0f, 1.0f},
};

constexpr auto kAmbientFields = [](auto&& field) {
    field("color", &AmbientLight::color);
    field("intensity", &AmbientLight::intensity);
    field("useSkyColor", &AmbientLight::useSkyColor);
    field("skyBlend", &AmbientLight::skyBlend);
};

constexpr auto kDirectionalFields = [](auto&& field) {
    field("enabled", &DirectionalLight::enabled);
    field("direction", &DirectionalLight::direction);
    field("intensity", &DirectionalLight::intensity);
    field("color", &DirectionalLight::color);
};

constexpr auto kDirectionalShadowFields = [](auto&& field) {
    using S = DirectionalShadowSettings;
    field("enabled", &S::enabled);
    field("resolution", &S::resolution);
    field("cascadeCount", &S::cascadeCount);
    field("cascadeSplitLambda", &S::cascadeSplitLambda);
    field("cascadeBlend", &S::cascadeBlend);
    field("nearPlane", &S::nearPlane);
    field("farPlane", &S::farPlane);
    field("depthBias", &S::depthBias);
    field("normalBias", &S::normalBias);
    field("strength", &S::strength);
    field("pcfEnabled", &S::pcfEnabled);
    field("pcfRadius", &S::pcfRadius);
    field("stabilize", &S::stabilize);
    field("showDebugTexture", &S::showDebugTexture);
    field("shadowDistance", &S::shadowDistance);
    field("showDebugFrustum", &S::showDebugFrustum);
};

// Point/Spot 共通。distanceFalloff と sourceRadius は専用の正規化を通す。
template <class Light>
constexpr auto kLocalLightFields = [](auto&& field) {
    field("enabled", &Light::enabled);
    field("position", &Light::position);
    field("range", &Light::range);
    field("color", &Light::color);
    field("intensity", &Light::intensity);
    field("priority", &Light::priority);
};

constexpr auto kSpotLightFields = [](auto&& field) {
    kLocalLightFields<SpotLight>(field);
    field("direction", &SpotLight::direction);
    field("innerConeAngleDegrees", &SpotLight::innerConeAngleDegrees);
    field("outerConeAngleDegrees", &SpotLight::outerConeAngleDegrees);
    field("angularFalloff", &SpotLight::angularFalloff);
};

constexpr auto kSkyFields = [](auto&& field) {
    using S = SkySettings;
    field("enabled", &S::enabled);
    field("skyAsset", &S::skyAsset);
    field("panoramaCloudEnabled", &S::panoramaCloudEnabled);
    field("panoramaCloudAsset", &S::panoramaCloudAsset);
    field("scale", &S::scale);
    field("yaw", &S::yaw);
    field("exposure", &S::exposure);
    field("tint", &S::tint);
    field("followCamera", &S::followCamera);
    field("zenithColor", &S::zenithColor);
    field("horizonColor", &S::horizonColor);
    field("groundColor", &S::groundColor);
    field("horizonPower", &S::horizonPower);
    field("showSunDisk", &S::showSunDisk);
    field("sunDiskIntensity", &S::sunDiskIntensity);
    field("sunDiskSize", &S::sunDiskSize);
    field("ambientFromSky", &S::ambientFromSky);
    field("reflectionIntensity", &S::reflectionIntensity);
    field("showDebugTexture", &S::showDebugTexture);
};

constexpr auto kReflectionProbeFields = [](auto&& field) {
    using S = ReflectionProbeSettings;
    field("enabled", &S::enabled);
    field("sourceCubemapAsset", &S::sourceCubemapAsset);
    field("position", &S::position);
    field("radius", &S::radius);
    field("intensity", &S::intensity);
    field("blendDistance", &S::blendDistance);
    field("priority", &S::priority);
};

constexpr auto kAmbientOcclusionFields = [](auto&& field) {
    using S = AmbientOcclusionSettings;
    field("radius", &S::radius);
    field("bias", &S::bias);
    field("strength", &S::strength);
    field("power", &S::power);
    field("diffuseStrength", &S::diffuseStrength);
    field("specularStrength", &S::specularStrength);
    field("sampleCount", &S::sampleCount);
    field("blurIterations", &S::blurIterations);
};

constexpr auto kScreenSpaceReflectionFields = [](auto&& field) {
    using S = ScreenSpaceReflectionSettings;
    field("enabled", &S::enabled);
    field("rayTracingFallback", &S::rayTracingFallback);
    field("intensity", &S::intensity);
    field("maxDistance", &S::maxDistance);
    field("thickness", &S::thickness);
    field("maxRoughness", &S::maxRoughness);
    field("edgeFade", &S::edgeFade);
    field("stride", &S::stride);
};

constexpr auto kBloomFields = [](auto&& field) {
    field("enabled", &BloomSettings::enabled);
    field("threshold", &BloomSettings::threshold);
    field("intensity", &BloomSettings::intensity);
    field("radius", &BloomSettings::radius);
    field("downsampleCount", &BloomSettings::downsampleCount);
};

constexpr auto kFogFields = [](auto&& field) {
    using S = FogSettings;
    field("enabled", &S::enabled);
    field("volumetric", &S::volumetric);
    field("color", &S::color);
    field("density", &S::density);
    field("startDistance", &S::startDistance);
    field("endDistance", &S::endDistance);
    field("heightFalloff", &S::heightFalloff);
    field("baseHeight", &S::baseHeight);
    field("anisotropy", &S::anisotropy);
    field("temporalWeight", &S::temporalWeight);
    field("scatteringIntensity", &S::scatteringIntensity);
    field("ambientScatteringStrength", &S::ambientScatteringStrength);
    field("sunShaftStrength", &S::sunShaftStrength);
    field("useSkyHorizonColor", &S::useSkyHorizonColor);
};

constexpr auto kToneMappingFields = [](auto&& field) {
    using S = ToneMappingSettings;
    field("enabled", &S::enabled);
    field("exposure", &S::exposure);
    field("whiteBalanceTemperature", &S::whiteBalanceTemperature);
    field("whiteBalanceTint", &S::whiteBalanceTint);
    field("contrast", &S::contrast);
    field("saturation", &S::saturation);
    field("gamma", &S::gamma);
    field("mode", &S::mode);
};

constexpr auto kAutomaticExposureFields = [](auto&& field) {
    using S = RENDER3D::COLOR::AutomaticExposureSettings;
    field("targetLuminance", &S::targetLuminance);
    field("compensationEv", &S::compensationEv);
    field("minExposureEv", &S::minExposureEv);
    field("maxExposureEv", &S::maxExposureEv);
    field("speedUp", &S::speedUp);
    field("speedDown", &S::speedDown);
};

// 名前付きの子オブジェクトがある時だけ読む
const nlohmann::json* FindObject(const nlohmann::json& node, const char* key) {
    const nlohmann::json* member = JSONREAD::FindMember(node, key);
    return member != nullptr && member->is_object() ? member : nullptr;
}

template <class Light, class Fields>
nlohmann::json WriteLocalLight(const Light& light, const Fields& fields) {
    nlohmann::json out = nlohmann::json::object();
    JSONFIELDS::Write(out, light, fields);
    out["distanceFalloff"] = RENDER3D::LIGHTING::ToString(light.distanceFalloff);
    out["sourceRadius"] = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(light.sourceRadius);
    return out;
}

template <class Light, class Fields>
Light ReadLocalLight(const nlohmann::json& node, const Fields& fields) {
    Light light{};
    JSONFIELDS::Read(node, light, fields);
    light.distanceFalloff =
        RENDER3D::LIGHTING::ParseLocalLightFalloff(node.value("distanceFalloff", std::string("LegacyRange")));
    light.sourceRadius = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(node.value("sourceRadius", 0.0f));
    return light;
}

} // namespace

void SerializeSceneEnvironmentJson(const SceneEnvironment& environment, nlohmann::json& out) {
    out = nlohmann::json::object();
    out["schemaVersion"] = 2u;
    JSONFIELDS::Write(out["ambient"], environment.ambient, kAmbientFields);
    JSONFIELDS::Write(out["directional"], environment.directional, kDirectionalFields);
    JSONFIELDS::Write(out["directionalShadow"], environment.directionalShadow, kDirectionalShadowFields);

    out["pointLights"] = nlohmann::json::array();
    for (const PointLight& pointLight : environment.pointLights) {
        out["pointLights"].push_back(WriteLocalLight(pointLight, kLocalLightFields<PointLight>));
    }
    out["spotLights"] = nlohmann::json::array();
    for (const SpotLight& spotLight : environment.spotLights) {
        out["spotLights"].push_back(WriteLocalLight(spotLight, kSpotLightFields));
    }

    nlohmann::json& sky = out["sky"];
    JSONFIELDS::Write(sky, environment.sky, kSkyFields);
    sky["mode"] = ToString(environment.sky.mode);
    sky["panoramaCloudScrollSpeed"] = JsonMath::ToJsonArray(SanitizeCloudVec2(
        environment.sky.panoramaCloudScrollSpeed, SkySettings{}.panoramaCloudScrollSpeed, -1.0f, 1.0f));
    sky["panoramaCloudOpacity"] =
        SanitizeCloudFloat(environment.sky.panoramaCloudOpacity, SkySettings{}.panoramaCloudOpacity, 0.0f, 1.0f);

    const AtmosphereSettings atmosphereDefaults{};
    const AtmosphereSettings& atmosphere = environment.atmosphere;
    out["atmosphere"]["enabled"] = atmosphere.enabled;
    WriteEnvironmentFloats(out["atmosphere"], atmosphere, kAtmosphereSettingsFields);
    out["atmosphere"]["rayleighScattering"] = JsonMath::ToJsonArray(
        SanitizeCloudVec3(atmosphere.rayleighScattering, atmosphereDefaults.rayleighScattering, 0.0f, 1.0f));
    out["atmosphere"]["mieScattering"] = JsonMath::ToJsonArray(
        SanitizeCloudVec3(atmosphere.mieScattering, atmosphereDefaults.mieScattering, 0.0f, 1.0f));
    out["atmosphere"]["ozoneAbsorption"] = JsonMath::ToJsonArray(
        SanitizeCloudVec3(atmosphere.ozoneAbsorption, atmosphereDefaults.ozoneAbsorption, 0.0f, 1.0f));
    out["atmosphere"]["transmittanceLutWidth"] = atmosphere.transmittanceLutWidth;
    out["atmosphere"]["transmittanceLutHeight"] = atmosphere.transmittanceLutHeight;
    out["atmosphere"]["multiScatteringLutSize"] = atmosphere.multiScatteringLutSize;
    out["atmosphere"]["skyViewLutWidth"] = atmosphere.skyViewLutWidth;
    out["atmosphere"]["skyViewLutHeight"] = atmosphere.skyViewLutHeight;
    out["atmosphere"]["preferLut"] = atmosphere.preferLut;

    out["sunGlare"]["enabled"] = environment.sunGlare.enabled;
    WriteEnvironmentFloats(out["sunGlare"], environment.sunGlare, kSunGlareSettingsFields);

    const StylizedCloudSettings cloudDefaults{};
    const StylizedCloudSettings& clouds = environment.clouds;
    out["clouds"]["enabled"] = clouds.enabled;
    WriteEnvironmentFloats(out["clouds"], clouds, kStylizedCloudSettingsFields);
    out["clouds"]["windDirection"] = JsonMath::ToJsonArray(
        SanitizeCloudVec2(clouds.windDirection, cloudDefaults.windDirection, kCloudDirectionMin, kCloudDirectionMax));
    out["clouds"]["physicallyBasedLighting"] = clouds.physicallyBasedLighting;
    out["clouds"]["brightColor"] = JsonMath::ToJsonArray(
        SanitizeCloudVec3(clouds.brightColor, cloudDefaults.brightColor, kCloudColorMin, kCloudColorMax));
    out["clouds"]["shadowColor"] = JsonMath::ToJsonArray(
        SanitizeCloudVec3(clouds.shadowColor, cloudDefaults.shadowColor, kCloudColorMin, kCloudColorMax));

    const ReflectionProbeSettings& probe = environment.reflectionProbe;
    nlohmann::json& probeNode = out["reflectionProbe"];
    JSONFIELDS::Write(probeNode, probe, kReflectionProbeFields);
    probeNode["influenceShape"] = ToString(probe.influenceShape);
    probeNode["influenceBoxCenter"] = JsonMath::ToJsonArray(probe.influenceBoxCenter);
    probeNode["influenceBoxSize"] = JsonMath::ToJsonArray(probe.influenceBoxSize);
    probeNode["projectionShape"] = ToString(probe.projectionShape);
    probeNode["projectionBoxCenter"] = JsonMath::ToJsonArray(probe.projectionBoxCenter);
    probeNode["projectionBoxSize"] = JsonMath::ToJsonArray(probe.projectionBoxSize);

    JSONFIELDS::Write(out["ambientOcclusion"], environment.ambientOcclusion, kAmbientOcclusionFields);
    out["ambientOcclusion"]["enabled"] = environment.ambientOcclusion.enabled;
    out["ambientOcclusion"]["mode"] = GetSsaoModeName(environment.ambientOcclusion.mode);

    out["surfaceLightmaps"]["enabled"] = environment.surfaceLightmaps.enabled;

    JSONFIELDS::Write(out["screenSpaceReflections"], environment.screenSpaceReflections, kScreenSpaceReflectionFields);
    out["screenSpaceReflections"]["quality"] = ToString(environment.screenSpaceReflections.quality);

    JSONFIELDS::Write(out["bloom"], environment.bloom, kBloomFields);
    JSONFIELDS::Write(out["fog"], environment.fog, kFogFields);

    nlohmann::json& toneMapping = out["toneMapping"];
    JSONFIELDS::Write(toneMapping, environment.toneMapping, kToneMappingFields);
    toneMapping["exposureMode"] = ExposureModeName(environment.toneMapping.exposureMode);
    JSONFIELDS::Write(toneMapping["automaticExposure"], environment.toneMapping.automaticExposure,
                      kAutomaticExposureFields);

    out["showLightDebug"] = environment.showLightDebug;
    out["showSkyDebugInfo"] = environment.showSkyDebugInfo;

    out["post"]["enabled"] = environment.post.enabled;
    out["post"]["globalPostProfileId"] = environment.post.globalPostProfileId;
    out["post"]["valuesInitialized"] = environment.post.valuesInitialized;
    out["post"]["paramValues"] = nlohmann::json::array();
    for (const DirectX::XMFLOAT4& value : environment.post.paramValues) {
        out["post"]["paramValues"].push_back(JsonMath::ToJsonArray(value));
    }
}

void DeserializeSceneEnvironmentJson(const nlohmann::json& node, SceneEnvironment& outEnvironment) {
    // Clouds were introduced after the original environment format. A
    // missing or malformed member must keep the legacy scene disabled.
    outEnvironment.clouds = StylizedCloudSettings{};
    if (!node.is_object()) {
        return;
    }

    if (node.contains("ambient")) {
        JSONFIELDS::Read(node["ambient"], outEnvironment.ambient, kAmbientFields);
    }
    if (node.contains("directional")) {
        JSONFIELDS::Read(node["directional"], outEnvironment.directional, kDirectionalFields);
    }
    if (const nlohmann::json* shadow = FindObject(node, "directionalShadow")) {
        DirectionalShadowSettings& settings = outEnvironment.directionalShadow;
        JSONFIELDS::Read(*shadow, settings, kDirectionalShadowFields);
        settings.cascadeCount = std::clamp(settings.cascadeCount, 1u, SHADOW::kMaxDirectionalShadowCascades);
        settings.cascadeSplitLambda = std::clamp(settings.cascadeSplitLambda, 0.0f, 1.0f);
        settings.cascadeBlend = std::clamp(settings.cascadeBlend, 0.0f, 0.3f);
    }

    if (node.contains("pointLights") && node["pointLights"].is_array()) {
        outEnvironment.pointLights.clear();
        for (const nlohmann::json& pointNode : node["pointLights"]) {
            outEnvironment.pointLights.push_back(ReadLocalLight<PointLight>(pointNode, kLocalLightFields<PointLight>));
        }
    }
    if (node.contains("spotLights") && node["spotLights"].is_array()) {
        outEnvironment.spotLights.clear();
        for (const nlohmann::json& spotNode : node["spotLights"]) {
            SpotLight spotLight = ReadLocalLight<SpotLight>(spotNode, kSpotLightFields);
            RENDER3D::LIGHTING::SanitizeSpotLight(spotLight);
            outEnvironment.spotLights.push_back(spotLight);
        }
    }

    if (const nlohmann::json* sky = FindObject(node, "sky")) {
        SkySettings& settings = outEnvironment.sky;
        JSONFIELDS::Read(*sky, settings, kSkyFields);
        settings.mode = ParseSkyMode(sky->value("mode", nlohmann::json{}), settings.mode);
        settings.panoramaCloudScrollSpeed =
            ReadCloudVec2(sky->value("panoramaCloudScrollSpeed", nlohmann::json::array()),
                          settings.panoramaCloudScrollSpeed, -1.0f, 1.0f);
        settings.panoramaCloudOpacity =
            ReadCloudMember(*sky, "panoramaCloudOpacity", settings.panoramaCloudOpacity, 0.0f, 1.0f);
    } else if (node.contains("skyMode")) {
        outEnvironment.sky.mode = ParseSkyMode(node["skyMode"], outEnvironment.sky.mode);
    }

    // Atmosphere is additive to the legacy Gradient/Cubemap/Texture2D
    // payload. Missing members intentionally retain standard defaults so
    // old scenes deserialize without requiring a migration write-back.
    if (const nlohmann::json* atmosphereNode = FindObject(node, "atmosphere")) {
        const AtmosphereSettings defaults{};
        AtmosphereSettings& atmosphere = outEnvironment.atmosphere;
        atmosphere.enabled = JSONREAD::BooleanMemberOr(*atmosphereNode, "enabled", atmosphere.enabled);
        ReadEnvironmentFloats(*atmosphereNode, atmosphere, kAtmosphereSettingsFields);
        atmosphere.rayleighScattering = ReadAtmosphereVec3(
            atmosphereNode->value("rayleighScattering", nlohmann::json{}), defaults.rayleighScattering, 0.0f, 1.0f);
        atmosphere.mieScattering = ReadAtmosphereVec3(atmosphereNode->value("mieScattering", nlohmann::json{}),
                                                      defaults.mieScattering, 0.0f, 1.0f);
        atmosphere.ozoneAbsorption = ReadAtmosphereVec3(atmosphereNode->value("ozoneAbsorption", nlohmann::json{}),
                                                        defaults.ozoneAbsorption, 0.0f, 1.0f);
        atmosphere.transmittanceLutWidth =
            ReadAtmosphereUInt(*atmosphereNode, "transmittanceLutWidth", defaults.transmittanceLutWidth, 1u, 4096u);
        atmosphere.transmittanceLutHeight =
            ReadAtmosphereUInt(*atmosphereNode, "transmittanceLutHeight", defaults.transmittanceLutHeight, 1u, 4096u);
        atmosphere.multiScatteringLutSize =
            ReadAtmosphereUInt(*atmosphereNode, "multiScatteringLutSize", defaults.multiScatteringLutSize, 1u, 4096u);
        atmosphere.skyViewLutWidth =
            ReadAtmosphereUInt(*atmosphereNode, "skyViewLutWidth", defaults.skyViewLutWidth, 1u, 4096u);
        atmosphere.skyViewLutHeight =
            ReadAtmosphereUInt(*atmosphereNode, "skyViewLutHeight", defaults.skyViewLutHeight, 1u, 4096u);
        atmosphere.preferLut = JSONREAD::BooleanMemberOr(*atmosphereNode, "preferLut", defaults.preferLut);
    }

    if (const nlohmann::json* glareNode = FindObject(node, "sunGlare")) {
        SunGlareSettings& glare = outEnvironment.sunGlare;
        glare.enabled = JSONREAD::BooleanMemberOr(*glareNode, "enabled", glare.enabled);
        ReadEnvironmentFloats(*glareNode, glare, kSunGlareSettingsFields);
    }

    if (const nlohmann::json* cloudNode = FindObject(node, "clouds")) {
        StylizedCloudSettings& clouds = outEnvironment.clouds;
        const StylizedCloudSettings defaults{};
        clouds.enabled = JSONREAD::BooleanMemberOr(*cloudNode, "enabled", defaults.enabled);
        // 旧 scene の色段階表現は保持し、新規 scene は物理散乱を既定にする。
        clouds.physicallyBasedLighting = JSONREAD::BooleanMemberOr(*cloudNode, "physicallyBasedLighting", false);
        ReadEnvironmentFloats(*cloudNode, clouds, kStylizedCloudSettingsFields);
        clouds.windDirection = ReadCloudVec2(cloudNode->value("windDirection", nlohmann::json{}),
                                             defaults.windDirection, kCloudDirectionMin, kCloudDirectionMax);
        clouds.brightColor = ReadCloudVec3(cloudNode->value("brightColor", nlohmann::json{}), defaults.brightColor,
                                           kCloudColorMin, kCloudColorMax);
        clouds.shadowColor = ReadCloudVec3(cloudNode->value("shadowColor", nlohmann::json{}), defaults.shadowColor,
                                           kCloudColorMin, kCloudColorMax);
    }

    if (const nlohmann::json* probeNode = FindObject(node, "reflectionProbe")) {
        const nlohmann::json& probe = *probeNode;
        ReflectionProbeSettings& settings = outEnvironment.reflectionProbe;
        JSONFIELDS::Read(probe, settings, kReflectionProbeFields);
        settings.influenceShape = ParseProbeShape(probe.value("influenceShape", nlohmann::json{}), kInfluenceShapeNames,
                                                  settings.influenceShape);
        settings.projectionShape = ParseProbeShape(probe.value("projectionShape", nlohmann::json{}),
                                                   kProjectionShapeNames, settings.projectionShape);
        // 箱の既定値は旧形式の中心位置と半径から作る
        const MATH::Vec3 radiusBoxSize = ReflectionProbeRadiusBoxSize(settings.radius);
        const auto readBox = [&](const char* key, MATH::Vec3& value, const MATH::Vec3& legacy) {
            value = JSONREAD::Vec3Or(probe.value(key, nlohmann::json::array()), probe.contains(key) ? value : legacy);
        };
        readBox("influenceBoxCenter", settings.influenceBoxCenter, settings.position);
        readBox("influenceBoxSize", settings.influenceBoxSize, radiusBoxSize);
        readBox("projectionBoxCenter", settings.projectionBoxCenter, settings.position);
        readBox("projectionBoxSize", settings.projectionBoxSize, radiusBoxSize);
    }

    if (const nlohmann::json* aoNode = FindObject(node, "ambientOcclusion")) {
        const nlohmann::json& ao = *aoNode;
        AmbientOcclusionSettings& settings = outEnvironment.ambientOcclusion;
        const bool legacyEnabled = ao.value("enabled", settings.enabled);
        if (ao.contains("mode")) {
            settings.mode = ParseSsaoMode(ao.value("mode", nlohmann::json{}), settings.mode);
            settings.enabled = settings.mode != SsaoMode::Off;
        } else {
            settings.enabled = legacyEnabled;
            settings.mode = legacyEnabled ? settings.mode : SsaoMode::Off;
        }
        JSONFIELDS::Read(ao, settings, kAmbientOcclusionFields);
        settings.sampleCount = std::clamp<uint32_t>(settings.sampleCount, 8u, 32u);
        settings.blurIterations = std::clamp<uint32_t>(settings.blurIterations, 0u, 4u);
    }

    if (const nlohmann::json* lightmaps = FindObject(node, "surfaceLightmaps")) {
        outEnvironment.surfaceLightmaps.enabled = lightmaps->value("enabled", outEnvironment.surfaceLightmaps.enabled);
    }

    if (const nlohmann::json* ssr = FindObject(node, "screenSpaceReflections")) {
        ScreenSpaceReflectionSettings& settings = outEnvironment.screenSpaceReflections;
        JSONFIELDS::Read(*ssr, settings, kScreenSpaceReflectionFields);
        settings.quality = ParseSsrQuality(ssr->value("quality", nlohmann::json{}), settings.quality);
        settings.intensity = std::clamp(settings.intensity, 0.0f, 2.0f);
        settings.maxDistance = std::clamp(settings.maxDistance, 1.0f, 200.0f);
        settings.thickness = std::clamp(settings.thickness, 0.0001f, 0.05f);
        settings.maxRoughness = std::clamp(settings.maxRoughness, 0.05f, 1.0f);
        settings.edgeFade = std::clamp(settings.edgeFade, 0.01f, 0.35f);
        settings.stride = std::clamp(settings.stride, 0.02f, 2.0f);
    }

    if (const nlohmann::json* bloom = FindObject(node, "bloom")) {
        JSONFIELDS::Read(*bloom, outEnvironment.bloom, kBloomFields);
    }
    if (const nlohmann::json* fog = FindObject(node, "fog")) {
        JSONFIELDS::Read(*fog, outEnvironment.fog, kFogFields);
    }

    if (const nlohmann::json* toneMapping = FindObject(node, "toneMapping")) {
        ToneMappingSettings& settings = outEnvironment.toneMapping;
        JSONFIELDS::Read(*toneMapping, settings, kToneMappingFields);
        if (toneMapping->contains("exposureMode")) {
            settings.exposureMode = ParseExposureMode((*toneMapping)["exposureMode"], settings.exposureMode);
        }
        if (const nlohmann::json* automaticExposure = FindObject(*toneMapping, "automaticExposure")) {
            JSONFIELDS::Read(*automaticExposure, settings.automaticExposure, kAutomaticExposureFields);
        }
    }

    // 旧 Phong の specularIntensity/specularPower は PBR では使わない。
    // 既存 Scene に残るキーは他の未知キーと同様に読み飛ばす。
    outEnvironment.showLightDebug = node.value("showLightDebug", outEnvironment.showLightDebug);
    outEnvironment.showSkyDebugInfo = node.value("showSkyDebugInfo", outEnvironment.showSkyDebugInfo);

    if (const nlohmann::json* postNode = FindObject(node, "post")) {
        const nlohmann::json& post = *postNode;
        outEnvironment.post.enabled = post.value("enabled", outEnvironment.post.enabled);
        outEnvironment.post.globalPostProfileId =
            post.value("globalPostProfileId", outEnvironment.post.globalPostProfileId);
        // paramValues が無い旧形式は userOverrides を読む
        const nlohmann::json* values = JSONREAD::FindMember(post, "paramValues");
        if (values == nullptr || !values->is_array()) {
            values = JSONREAD::FindMember(post, "userOverrides");
        }
        const bool hasParamValues = values != nullptr && values->is_array();
        if (hasParamValues) {
            const size_t count = std::min<size_t>(values->size(), std::size(outEnvironment.post.paramValues));
            for (size_t i = 0; i < count; ++i) {
                outEnvironment.post.paramValues[i] =
                    DeserializeFloat4((*values)[i], outEnvironment.post.paramValues[i]);
            }
        }
        outEnvironment.post.valuesInitialized = post.value("valuesInitialized", hasParamValues);
    }
}

} // namespace HIKARI::SCENE::SERIALIZATION
