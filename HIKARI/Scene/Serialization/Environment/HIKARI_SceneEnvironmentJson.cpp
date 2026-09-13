#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>

#include "Core/HIKARI_JsonRead.h"
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
            return { node[0].is_number() ? node[0].get<float>() : fallback.x, node[1].is_number() ? node[1].get<float>() : fallback.y,
                     node[2].is_number() ? node[2].get<float>() : fallback.z, node[3].is_number() ? node[3].get<float>() : fallback.w };
        }

        const char* ToString(SkyMode mode) {
            switch (mode) {
            case SkyMode::None:
                return "None";
            case SkyMode::Cubemap:
                return "Cubemap";
            case SkyMode::Texture2D:
                return "Texture2D";
            case SkyMode::Atmosphere:
                return "Atmosphere";
            case SkyMode::Gradient:
            default:
                return "Gradient";
            }
        }

        SkyMode ParseSkyMode(const nlohmann::json& node, SkyMode fallback) {

            if (node.is_number_integer()) {
                const int value = node.get<int>();
                if (value >= static_cast<int>(SkyMode::None) && value <= static_cast<int>(SkyMode::Atmosphere)) {
                    return static_cast<SkyMode>(value);
                }
                return fallback;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "None") return SkyMode::None;
            if (value == "Gradient") return SkyMode::Gradient;
            if (value == "Cubemap") return SkyMode::Cubemap;
            if (value == "Texture2D") return SkyMode::Texture2D;
            if (value == "Atmosphere" || value == "Atmospheric" || value == "Physical") {
                return SkyMode::Atmosphere;
            }
            return fallback;
        }

        const char* ExposureModeName(
            RENDER3D::COLOR::ExposureMode mode) {
            switch (mode) {
            case RENDER3D::COLOR::ExposureMode::Automatic:
                return "Automatic";
            case RENDER3D::COLOR::ExposureMode::Manual:
            default:
                return "Manual";
            }
        }

        RENDER3D::COLOR::ExposureMode ParseExposureMode(
            const nlohmann::json& node,
            RENDER3D::COLOR::ExposureMode fallback) {

            if (node.is_number_integer()) {
                return node.get<int>() ==
                        static_cast<int>(
                            RENDER3D::COLOR::ExposureMode::Automatic)
                    ? RENDER3D::COLOR::ExposureMode::Automatic
                    : RENDER3D::COLOR::ExposureMode::Manual;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "Automatic" || value == "automatic") {
                return RENDER3D::COLOR::ExposureMode::Automatic;
            }
            if (value == "Manual" || value == "manual") {
                return RENDER3D::COLOR::ExposureMode::Manual;
            }
            return fallback;
        }

        const char* ToString(ReflectionProbeInfluenceShape shape) {

            switch (shape) {
            case ReflectionProbeInfluenceShape::Box:
                return "Box";
            case ReflectionProbeInfluenceShape::Sphere:
            default:
                return "Sphere";
            }
        }

        const char* ToString(ReflectionProbeProjectionShape shape) {

            switch (shape) {
            case ReflectionProbeProjectionShape::Box:
                return "Box";
            case ReflectionProbeProjectionShape::Infinite:
            default:
                return "Infinite";
            }
        }

        ReflectionProbeInfluenceShape ParseReflectionProbeInfluenceShape(const nlohmann::json& node,
                                                                         ReflectionProbeInfluenceShape fallback) {

            if (node.is_number_integer()) {
                return node.get<int>() == 1 ? ReflectionProbeInfluenceShape::Box : fallback;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "Box") {
                return ReflectionProbeInfluenceShape::Box;
            }
            if (value == "Sphere") {
                return ReflectionProbeInfluenceShape::Sphere;
            }
            return fallback;
        }

        ReflectionProbeProjectionShape ParseReflectionProbeProjectionShape(const nlohmann::json& node,
                                                                           ReflectionProbeProjectionShape fallback) {

            if (node.is_number_integer()) {
                return node.get<int>() == 1 ? ReflectionProbeProjectionShape::Box : fallback;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "Box") {
                return ReflectionProbeProjectionShape::Box;
            }
            if (value == "Infinite") {
                return ReflectionProbeProjectionShape::Infinite;
            }
            return fallback;
        }

        SsaoMode ParseSsaoMode(const nlohmann::json& node, SsaoMode fallback) {

            if (node.is_number_integer()) {
                const int value = node.get<int>();
                if (value >= static_cast<int>(SsaoMode::Off) && value <= static_cast<int>(SsaoMode::Balanced)) {
                    return static_cast<SsaoMode>(value);
                }
                return fallback;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "Off") return SsaoMode::Off;
            if (value == "Reference") return SsaoMode::Reference;
            if (value == "OptimizedHigh") {
                return SsaoMode::OptimizedHigh;
            }
            if (value == "Balanced") return SsaoMode::Balanced;
            return fallback;
        }

        const char* ToString(ScreenSpaceReflectionQuality quality) {
            switch (quality) {
            case ScreenSpaceReflectionQuality::Off:
                return "Off";
            case ScreenSpaceReflectionQuality::Low:
                return "Low";
            case ScreenSpaceReflectionQuality::High:
                return "High";
            case ScreenSpaceReflectionQuality::Medium:
            default:
                return "Medium";
            }
        }

        ScreenSpaceReflectionQuality ParseSsrQuality(
            const nlohmann::json& node,
            ScreenSpaceReflectionQuality fallback) {
            if (node.is_number_integer()) {
                const int value = node.get<int>();
                if (value >=
                        static_cast<int>(
                            ScreenSpaceReflectionQuality::Off) &&
                    value <=
                        static_cast<int>(
                            ScreenSpaceReflectionQuality::High)) {
                    return static_cast<ScreenSpaceReflectionQuality>(value);
                }
                return fallback;
            }
            if (!node.is_string()) {
                return fallback;
            }
            const std::string value = node.get<std::string>();
            if (value == "Off") return ScreenSpaceReflectionQuality::Off;
            if (value == "Low") return ScreenSpaceReflectionQuality::Low;
            if (value == "Medium") {
                return ScreenSpaceReflectionQuality::Medium;
            }
            if (value == "High") return ScreenSpaceReflectionQuality::High;
            return fallback;
        }

        MATH::Vec3 ReflectionProbeRadiusBoxSize(float radius) {
            const float diameter = std::max(0.01f, radius * 2.0f);
            return { diameter, diameter, diameter };
        }

        constexpr float kCloudColorMin = 0.0f;
        constexpr float kCloudColorMax = 16.0f;
        constexpr float kCloudDirectionMin = -10000.0f;
        constexpr float kCloudDirectionMax = 10000.0f;

        float SanitizeCloudFloat(float value,
                                 float fallback,
                                 float minimum,
                                 float maximum) noexcept {
            if (!std::isfinite(value)) {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        float ReadCloudNumber(const nlohmann::json* value,
                              float fallback,
                              float minimum,
                              float maximum) {
            if (value == nullptr || !value->is_number()) {
                return fallback;
            }
            try {
                const double parsed = value->get<double>();
                if (!std::isfinite(parsed)) {
                    return fallback;
                }
                const double clamped = std::clamp(
                    parsed,
                    static_cast<double>(minimum),
                    static_cast<double>(maximum));
                const float result = static_cast<float>(clamped);
                return std::isfinite(result) ? result : fallback;
            } catch (...) {
                return fallback;
            }
        }

        float ReadCloudMember(const nlohmann::json& object,
                              const char* key,
                              float fallback,
                              float minimum,
                              float maximum) {
            return ReadCloudNumber(
                JSONREAD::FindMember(object, key),
                fallback,
                minimum,
                maximum);
        }

        float ReadCloudArrayElement(const nlohmann::json& node,
                                    std::size_t index,
                                    float fallback,
                                    float minimum,
                                    float maximum) {
            if (!node.is_array() || node.size() <= index) {
                return fallback;
            }
            return ReadCloudNumber(
                &node[index], fallback, minimum, maximum);
        }

        MATH::Vec2 ReadCloudVec2(const nlohmann::json& node,
                                 const MATH::Vec2& fallback,
                                 float minimum,
                                 float maximum) {
            MATH::Vec2 value = fallback;
            if (node.is_array()) {
                value.x = ReadCloudArrayElement(
                    node, 0u, value.x, minimum, maximum);
                value.y = ReadCloudArrayElement(
                    node, 1u, value.y, minimum, maximum);
            } else if (node.is_object()) {
                value.x = ReadCloudMember(
                    node, "x", value.x, minimum, maximum);
                value.y = ReadCloudMember(
                    node, "y", value.y, minimum, maximum);
            }
            return value;
        }

        MATH::Vec3 ReadCloudVec3(const nlohmann::json& node,
                                 const MATH::Vec3& fallback,
                                 float minimum,
                                 float maximum) {
            MATH::Vec3 value = fallback;
            if (node.is_array()) {
                value.x = ReadCloudArrayElement(
                    node, 0u, value.x, minimum, maximum);
                value.y = ReadCloudArrayElement(
                    node, 1u, value.y, minimum, maximum);
                value.z = ReadCloudArrayElement(
                    node, 2u, value.z, minimum, maximum);
            } else if (node.is_object()) {
                value.x = ReadCloudMember(
                    node, "x", value.x, minimum, maximum);
                value.y = ReadCloudMember(
                    node, "y", value.y, minimum, maximum);
                value.z = ReadCloudMember(
                    node, "z", value.z, minimum, maximum);
            }
            return value;
        }

        MATH::Vec2 SanitizeCloudVec2(const MATH::Vec2& value,
                                     const MATH::Vec2& fallback,
                                     float minimum,
                                     float maximum) noexcept {
            return {
                SanitizeCloudFloat(value.x, fallback.x, minimum, maximum),
                SanitizeCloudFloat(value.y, fallback.y, minimum, maximum),
            };
        }

        MATH::Vec3 SanitizeCloudVec3(const MATH::Vec3& value,
                                     const MATH::Vec3& fallback,
                                     float minimum,
                                     float maximum) noexcept {
            return {
                SanitizeCloudFloat(value.x, fallback.x, minimum, maximum),
                SanitizeCloudFloat(value.y, fallback.y, minimum, maximum),
                SanitizeCloudFloat(value.z, fallback.z, minimum, maximum),
            };
        }

        float ReadAtmosphereMember(
            const nlohmann::json& object,
            const char* key,
            float fallback,
            float minimum,
            float maximum) {
            return ReadCloudMember(object, key, fallback, minimum, maximum);
        }

        MATH::Vec3 ReadAtmosphereVec3(
            const nlohmann::json& node,
            const MATH::Vec3& fallback,
            float minimum,
            float maximum) {
            return ReadCloudVec3(node, fallback, minimum, maximum);
        }

        uint32_t ReadAtmosphereUInt(
            const nlohmann::json& object,
            const char* key,
            uint32_t fallback,
            uint32_t minimum,
            uint32_t maximum) {
            const float parsed = ReadAtmosphereMember(
                object,
                key,
                static_cast<float>(fallback),
                static_cast<float>(minimum),
                static_cast<float>(maximum));
            if (!std::isfinite(parsed)) {
                return fallback;
            }
            return static_cast<uint32_t>(parsed);
        }

    } // namespace

    void SerializeSceneEnvironmentJson(const SceneEnvironment& environment, nlohmann::json& out) {

        out = nlohmann::json::object();
        out["schemaVersion"] = 2u;
        out["ambient"]["color"] = JsonMath::ToJsonArray(environment.ambient.color);
        out["ambient"]["intensity"] = environment.ambient.intensity;
        out["ambient"]["useSkyColor"] = environment.ambient.useSkyColor;
        out["ambient"]["skyBlend"] = environment.ambient.skyBlend;

        out["directional"]["enabled"] = environment.directional.enabled;
        out["directional"]["direction"] = JsonMath::ToJsonArray(environment.directional.direction);
        out["directional"]["intensity"] = environment.directional.intensity;
        out["directional"]["color"] = JsonMath::ToJsonArray(environment.directional.color);

        out["directionalShadow"]["enabled"] = environment.directionalShadow.enabled;
        out["directionalShadow"]["resolution"] = environment.directionalShadow.resolution;
        out["directionalShadow"]["cascadeCount"] =
            environment.directionalShadow.cascadeCount;
        out["directionalShadow"]["cascadeSplitLambda"] =
            environment.directionalShadow.cascadeSplitLambda;
        out["directionalShadow"]["cascadeBlend"] =
            environment.directionalShadow.cascadeBlend;
        out["directionalShadow"]["nearPlane"] = environment.directionalShadow.nearPlane;
        out["directionalShadow"]["farPlane"] = environment.directionalShadow.farPlane;
        out["directionalShadow"]["depthBias"] = environment.directionalShadow.depthBias;
        out["directionalShadow"]["normalBias"] = environment.directionalShadow.normalBias;
        out["directionalShadow"]["strength"] = environment.directionalShadow.strength;
        out["directionalShadow"]["pcfEnabled"] = environment.directionalShadow.pcfEnabled;
        out["directionalShadow"]["pcfRadius"] = environment.directionalShadow.pcfRadius;
        out["directionalShadow"]["stabilize"] = environment.directionalShadow.stabilize;
        out["directionalShadow"]["showDebugTexture"] = environment.directionalShadow.showDebugTexture;
        out["directionalShadow"]["shadowDistance"] = environment.directionalShadow.shadowDistance;
        out["directionalShadow"]["showDebugFrustum"] = environment.directionalShadow.showDebugFrustum;

        out["pointLights"] = nlohmann::json::array();
        for (const PointLight& pointLight : environment.pointLights) {
            out["pointLights"].push_back({ { "enabled", pointLight.enabled },
                                           { "position", JsonMath::ToJsonArray(pointLight.position) },
                                           { "range", pointLight.range },
                { "distanceFalloff", RENDER3D::LIGHTING::ToString(pointLight.distanceFalloff) },
                                           { "color", JsonMath::ToJsonArray(pointLight.color) },
                                           { "intensity", pointLight.intensity },
                                           { "priority", pointLight.priority } });
        }

        out["spotLights"] = nlohmann::json::array();
        for (const SpotLight& spotLight : environment.spotLights) {
            out["spotLights"].push_back({
                { "enabled", spotLight.enabled },
                { "position", JsonMath::ToJsonArray(spotLight.position) },
                { "range", spotLight.range },
                { "distanceFalloff", RENDER3D::LIGHTING::ToString(spotLight.distanceFalloff) },
                { "direction", JsonMath::ToJsonArray(spotLight.direction) },
                { "innerConeAngleDegrees", spotLight.innerConeAngleDegrees },
                { "outerConeAngleDegrees", spotLight.outerConeAngleDegrees },
                { "angularFalloff", spotLight.angularFalloff },
                { "color", JsonMath::ToJsonArray(spotLight.color) },
                { "intensity", spotLight.intensity },
                { "priority", spotLight.priority }
            });
        }

        out["sky"]["enabled"] = environment.sky.enabled;
        out["sky"]["mode"] = ToString(environment.sky.mode);
        out["sky"]["skyAsset"] = environment.sky.skyAsset;
        out["sky"]["panoramaCloudEnabled"] =
            environment.sky.panoramaCloudEnabled;
        out["sky"]["panoramaCloudAsset"] =
            environment.sky.panoramaCloudAsset;
        out["sky"]["panoramaCloudScrollSpeed"] = JsonMath::ToJsonArray(
            SanitizeCloudVec2(
                environment.sky.panoramaCloudScrollSpeed,
                SkySettings{}.panoramaCloudScrollSpeed,
                -1.0f,
                1.0f));
        out["sky"]["panoramaCloudOpacity"] = SanitizeCloudFloat(
            environment.sky.panoramaCloudOpacity,
            SkySettings{}.panoramaCloudOpacity,
            0.0f,
            1.0f);
        out["sky"]["scale"] = environment.sky.scale;
        out["sky"]["yaw"] = environment.sky.yaw;
        out["sky"]["exposure"] = environment.sky.exposure;
        out["sky"]["tint"] = JsonMath::ToJsonArray(environment.sky.tint);
        out["sky"]["followCamera"] = environment.sky.followCamera;
        out["sky"]["zenithColor"] = JsonMath::ToJsonArray(environment.sky.zenithColor);
        out["sky"]["horizonColor"] = JsonMath::ToJsonArray(environment.sky.horizonColor);
        out["sky"]["groundColor"] = JsonMath::ToJsonArray(environment.sky.groundColor);
        out["sky"]["horizonPower"] = environment.sky.horizonPower;
        out["sky"]["showSunDisk"] = environment.sky.showSunDisk;
        out["sky"]["sunDiskIntensity"] = environment.sky.sunDiskIntensity;
        out["sky"]["sunDiskSize"] = environment.sky.sunDiskSize;
        out["sky"]["ambientFromSky"] = environment.sky.ambientFromSky;
        out["sky"]["reflectionIntensity"] = environment.sky.reflectionIntensity;
        out["sky"]["showDebugTexture"] = environment.sky.showDebugTexture;

        const AtmosphereSettings atmosphereDefaults{};
        const AtmosphereSettings& atmosphere = environment.atmosphere;
        out["atmosphere"]["enabled"] = atmosphere.enabled;
        out["atmosphere"]["planetRadius"] = SanitizeCloudFloat(
            atmosphere.planetRadius,
            atmosphereDefaults.planetRadius,
            1.0f,
            100000000.0f);
        out["atmosphere"]["atmosphereHeight"] = SanitizeCloudFloat(
            atmosphere.atmosphereHeight,
            atmosphereDefaults.atmosphereHeight,
            1.0f,
            1000000.0f);
        out["atmosphere"]["rayleighScaleHeight"] = SanitizeCloudFloat(
            atmosphere.rayleighScaleHeight,
            atmosphereDefaults.rayleighScaleHeight,
            1.0f,
            1000000.0f);
        out["atmosphere"]["mieScaleHeight"] = SanitizeCloudFloat(
            atmosphere.mieScaleHeight,
            atmosphereDefaults.mieScaleHeight,
            1.0f,
            1000000.0f);
        out["atmosphere"]["rayleighScattering"] = JsonMath::ToJsonArray(
            SanitizeCloudVec3(
                atmosphere.rayleighScattering,
                atmosphereDefaults.rayleighScattering,
                0.0f,
                1.0f));
        out["atmosphere"]["mieScattering"] = JsonMath::ToJsonArray(
            SanitizeCloudVec3(
                atmosphere.mieScattering,
                atmosphereDefaults.mieScattering,
                0.0f,
                1.0f));
        out["atmosphere"]["ozoneAbsorption"] = JsonMath::ToJsonArray(
            SanitizeCloudVec3(
                atmosphere.ozoneAbsorption,
                atmosphereDefaults.ozoneAbsorption,
                0.0f,
                1.0f));
        out["atmosphere"]["ozoneCenterHeight"] = SanitizeCloudFloat(
            atmosphere.ozoneCenterHeight,
            atmosphereDefaults.ozoneCenterHeight,
            0.0f,
            1000000.0f);
        out["atmosphere"]["ozoneWidth"] = SanitizeCloudFloat(
            atmosphere.ozoneWidth,
            atmosphereDefaults.ozoneWidth,
            1.0f,
            1000000.0f);
        out["atmosphere"]["mieAnisotropy"] = SanitizeCloudFloat(
            atmosphere.mieAnisotropy,
            atmosphereDefaults.mieAnisotropy,
            -0.95f,
            0.95f);
        out["atmosphere"]["sunIlluminance"] = SanitizeCloudFloat(
            atmosphere.sunIlluminance,
            atmosphereDefaults.sunIlluminance,
            0.0f,
            16.0f);
        out["atmosphere"]["sunExposure"] = SanitizeCloudFloat(
            atmosphere.sunExposure,
            atmosphereDefaults.sunExposure,
            -16.0f,
            16.0f);
        out["atmosphere"]["groundAlbedo"] = SanitizeCloudFloat(
            atmosphere.groundAlbedo,
            atmosphereDefaults.groundAlbedo,
            0.0f,
            1.0f);
        out["atmosphere"]["aerialPerspectiveStrength"] = SanitizeCloudFloat(
            atmosphere.aerialPerspectiveStrength,
            atmosphereDefaults.aerialPerspectiveStrength,
            0.0f,
            8.0f);
        out["atmosphere"]["sunAngularRadius"] = SanitizeCloudFloat(
            atmosphere.sunAngularRadius,
            atmosphereDefaults.sunAngularRadius,
            0.0001f,
            0.25f);
        out["atmosphere"]["transmittanceLutWidth"] = atmosphere.transmittanceLutWidth;
        out["atmosphere"]["transmittanceLutHeight"] = atmosphere.transmittanceLutHeight;
        out["atmosphere"]["multiScatteringLutSize"] = atmosphere.multiScatteringLutSize;
        out["atmosphere"]["skyViewLutWidth"] = atmosphere.skyViewLutWidth;
        out["atmosphere"]["skyViewLutHeight"] = atmosphere.skyViewLutHeight;
        out["atmosphere"]["preferLut"] = atmosphere.preferLut;

        const SunGlareSettings glareDefaults{};
        const SunGlareSettings& glare = environment.sunGlare;
        out["sunGlare"]["enabled"] = glare.enabled;
        out["sunGlare"]["intensity"] = SanitizeCloudFloat(
            glare.intensity, glareDefaults.intensity, 0.0f, 8.0f);
        out["sunGlare"]["haloRadius"] = SanitizeCloudFloat(
            glare.haloRadius, glareDefaults.haloRadius, 0.0f, 2.0f);
        out["sunGlare"]["haloFalloff"] = SanitizeCloudFloat(
            glare.haloFalloff, glareDefaults.haloFalloff, 0.1f, 16.0f);
        out["sunGlare"]["streakIntensity"] = SanitizeCloudFloat(
            glare.streakIntensity, glareDefaults.streakIntensity, 0.0f, 8.0f);
        out["sunGlare"]["streakLength"] = SanitizeCloudFloat(
            glare.streakLength, glareDefaults.streakLength, 0.0f, 2.0f);
        out["sunGlare"]["occlusionRadius"] = SanitizeCloudFloat(
            glare.occlusionRadius, glareDefaults.occlusionRadius, 0.0f, 0.25f);
        out["sunGlare"]["edgeFade"] = SanitizeCloudFloat(
            glare.edgeFade, glareDefaults.edgeFade, 0.0f, 1.0f);
        out["sunGlare"]["maxRadiance"] = SanitizeCloudFloat(
            glare.maxRadiance, glareDefaults.maxRadiance, 0.0f, 64.0f);
        out["sunGlare"]["shaftIntensity"] = SanitizeCloudFloat(
            glare.shaftIntensity, glareDefaults.shaftIntensity, 0.0f, 8.0f);
        out["sunGlare"]["shaftLength"] = SanitizeCloudFloat(
            glare.shaftLength, glareDefaults.shaftLength, 0.01f, 2.0f);
        out["sunGlare"]["shaftDecay"] = SanitizeCloudFloat(
            glare.shaftDecay, glareDefaults.shaftDecay, 0.5f, 0.999f);

        const StylizedCloudSettings cloudDefaults{};
        const StylizedCloudSettings& clouds = environment.clouds;
        out["clouds"]["enabled"] = clouds.enabled;
        out["clouds"]["coverage"] = SanitizeCloudFloat(
            clouds.coverage, cloudDefaults.coverage, 0.0f, 1.0f);
        out["clouds"]["density"] = SanitizeCloudFloat(
            clouds.density, cloudDefaults.density, 0.0f, 8.0f);
        out["clouds"]["altitude"] = SanitizeCloudFloat(
            clouds.altitude, cloudDefaults.altitude, 0.0f, 100000.0f);
        out["clouds"]["thickness"] = SanitizeCloudFloat(
            clouds.thickness, cloudDefaults.thickness, 0.1f, 100000.0f);
        out["clouds"]["scale"] = SanitizeCloudFloat(
            clouds.scale, cloudDefaults.scale, 0.1f, 100000.0f);
        out["clouds"]["windDirection"] = JsonMath::ToJsonArray(
            SanitizeCloudVec2(
                clouds.windDirection,
                cloudDefaults.windDirection,
                kCloudDirectionMin,
                kCloudDirectionMax));
        out["clouds"]["windSpeed"] = SanitizeCloudFloat(
            clouds.windSpeed, cloudDefaults.windSpeed, 0.0f, 1000.0f);
        out["clouds"]["shapeDetail"] = SanitizeCloudFloat(
            clouds.shapeDetail, cloudDefaults.shapeDetail, 0.0f, 1.0f);
        out["clouds"]["softness"] = SanitizeCloudFloat(
            clouds.softness, cloudDefaults.softness, 0.0f, 1.0f);
        out["clouds"]["lightAbsorption"] = SanitizeCloudFloat(
            clouds.lightAbsorption, cloudDefaults.lightAbsorption, 0.0f, 8.0f);
        out["clouds"]["silverLining"] = SanitizeCloudFloat(
            clouds.silverLining, cloudDefaults.silverLining, 0.0f, 4.0f);
        out["clouds"]["horizonFade"] = SanitizeCloudFloat(
            clouds.horizonFade, cloudDefaults.horizonFade, 0.0f, 1.0f);
        out["clouds"]["brightColor"] = JsonMath::ToJsonArray(
            SanitizeCloudVec3(
                clouds.brightColor,
                cloudDefaults.brightColor,
                kCloudColorMin,
                kCloudColorMax));
        out["clouds"]["shadowColor"] = JsonMath::ToJsonArray(
            SanitizeCloudVec3(
                clouds.shadowColor,
                cloudDefaults.shadowColor,
                kCloudColorMin,
                kCloudColorMax));

        out["reflectionProbe"]["enabled"] = environment.reflectionProbe.enabled;
        out["reflectionProbe"]["sourceCubemapAsset"] = environment.reflectionProbe.sourceCubemapAsset;
        out["reflectionProbe"]["position"] = JsonMath::ToJsonArray(environment.reflectionProbe.position);
        out["reflectionProbe"]["radius"] = environment.reflectionProbe.radius;
        out["reflectionProbe"]["intensity"] = environment.reflectionProbe.intensity;
        out["reflectionProbe"]["influenceShape"] = ToString(environment.reflectionProbe.influenceShape);
        out["reflectionProbe"]["influenceBoxCenter"] = JsonMath::ToJsonArray(environment.reflectionProbe.influenceBoxCenter);
        out["reflectionProbe"]["influenceBoxSize"] = JsonMath::ToJsonArray(environment.reflectionProbe.influenceBoxSize);
        out["reflectionProbe"]["projectionShape"] = ToString(environment.reflectionProbe.projectionShape);
        out["reflectionProbe"]["projectionBoxCenter"] = JsonMath::ToJsonArray(environment.reflectionProbe.projectionBoxCenter);
        out["reflectionProbe"]["projectionBoxSize"] = JsonMath::ToJsonArray(environment.reflectionProbe.projectionBoxSize);
        out["reflectionProbe"]["blendDistance"] = environment.reflectionProbe.blendDistance;
        out["reflectionProbe"]["priority"] = environment.reflectionProbe.priority;

        out["ambientOcclusion"]["enabled"] = environment.ambientOcclusion.enabled;
        out["ambientOcclusion"]["mode"] =
            GetSsaoModeName(environment.ambientOcclusion.mode);
        out["ambientOcclusion"]["radius"] = environment.ambientOcclusion.radius;
        out["ambientOcclusion"]["bias"] = environment.ambientOcclusion.bias;
        out["ambientOcclusion"]["strength"] = environment.ambientOcclusion.strength;
        out["ambientOcclusion"]["power"] = environment.ambientOcclusion.power;
        out["ambientOcclusion"]["diffuseStrength"] = environment.ambientOcclusion.diffuseStrength;
        out["ambientOcclusion"]["specularStrength"] = environment.ambientOcclusion.specularStrength;
        out["ambientOcclusion"]["sampleCount"] = environment.ambientOcclusion.sampleCount;
        out["ambientOcclusion"]["blurIterations"] = environment.ambientOcclusion.blurIterations;

        out["surfaceLightmaps"]["enabled"] =
            environment.surfaceLightmaps.enabled;

        const ScreenSpaceReflectionSettings& ssr =
            environment.screenSpaceReflections;
        out["screenSpaceReflections"]["enabled"] = ssr.enabled;
        out["screenSpaceReflections"]["quality"] =
            ToString(ssr.quality);
        out["screenSpaceReflections"]["intensity"] = ssr.intensity;
        out["screenSpaceReflections"]["maxDistance"] = ssr.maxDistance;
        out["screenSpaceReflections"]["thickness"] = ssr.thickness;
        out["screenSpaceReflections"]["maxRoughness"] = ssr.maxRoughness;
        out["screenSpaceReflections"]["edgeFade"] = ssr.edgeFade;
        out["screenSpaceReflections"]["stride"] = ssr.stride;

        out["bloom"]["enabled"] = environment.bloom.enabled;
        out["bloom"]["threshold"] = environment.bloom.threshold;
        out["bloom"]["intensity"] = environment.bloom.intensity;
        out["bloom"]["radius"] = environment.bloom.radius;
        out["bloom"]["downsampleCount"] = environment.bloom.downsampleCount;

        out["fog"]["enabled"] = environment.fog.enabled;
        out["fog"]["volumetric"] = environment.fog.volumetric;
        out["fog"]["color"] = JsonMath::ToJsonArray(environment.fog.color);
        out["fog"]["density"] = environment.fog.density;
        out["fog"]["startDistance"] = environment.fog.startDistance;
        out["fog"]["endDistance"] = environment.fog.endDistance;
        out["fog"]["heightFalloff"] = environment.fog.heightFalloff;
        out["fog"]["baseHeight"] = environment.fog.baseHeight;
        out["fog"]["anisotropy"] = environment.fog.anisotropy;
        out["fog"]["temporalWeight"] = environment.fog.temporalWeight;
        out["fog"]["scatteringIntensity"] =
            environment.fog.scatteringIntensity;
        out["fog"]["ambientScatteringStrength"] =
            environment.fog.ambientScatteringStrength;
        out["fog"]["sunShaftStrength"] =
            environment.fog.sunShaftStrength;
        out["fog"]["useSkyHorizonColor"] = environment.fog.useSkyHorizonColor;

        out["toneMapping"]["enabled"] = environment.toneMapping.enabled;
        out["toneMapping"]["exposureMode"] =
            ExposureModeName(environment.toneMapping.exposureMode);
        out["toneMapping"]["exposure"] = environment.toneMapping.exposure;
        out["toneMapping"]["automaticExposure"]["targetLuminance"] =
            environment.toneMapping.automaticExposure.targetLuminance;
        out["toneMapping"]["automaticExposure"]["compensationEv"] =
            environment.toneMapping.automaticExposure.compensationEv;
        out["toneMapping"]["automaticExposure"]["minExposureEv"] =
            environment.toneMapping.automaticExposure.minExposureEv;
        out["toneMapping"]["automaticExposure"]["maxExposureEv"] =
            environment.toneMapping.automaticExposure.maxExposureEv;
        out["toneMapping"]["automaticExposure"]["speedUp"] =
            environment.toneMapping.automaticExposure.speedUp;
        out["toneMapping"]["automaticExposure"]["speedDown"] =
            environment.toneMapping.automaticExposure.speedDown;
        out["toneMapping"]["whiteBalanceTemperature"] =
            environment.toneMapping.whiteBalanceTemperature;
        out["toneMapping"]["whiteBalanceTint"] =
            environment.toneMapping.whiteBalanceTint;
        out["toneMapping"]["contrast"] = environment.toneMapping.contrast;
        out["toneMapping"]["saturation"] = environment.toneMapping.saturation;
        out["toneMapping"]["gamma"] = environment.toneMapping.gamma;
        out["toneMapping"]["mode"] = environment.toneMapping.mode;

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
            const nlohmann::json& ambient = node["ambient"];
            outEnvironment.ambient.color = JSONREAD::Vec3Or(ambient.value("color", nlohmann::json::array()), outEnvironment.ambient.color);
            outEnvironment.ambient.intensity = ambient.value("intensity", outEnvironment.ambient.intensity);
            outEnvironment.ambient.useSkyColor = ambient.value("useSkyColor", outEnvironment.ambient.useSkyColor);
            outEnvironment.ambient.skyBlend = ambient.value("skyBlend", outEnvironment.ambient.skyBlend);
        }

        if (node.contains("directional")) {
            const nlohmann::json& directional = node["directional"];
            outEnvironment.directional.enabled = directional.value("enabled", outEnvironment.directional.enabled);
            outEnvironment.directional.direction =
                JSONREAD::Vec3Or(directional.value("direction", nlohmann::json::array()), outEnvironment.directional.direction);
            outEnvironment.directional.intensity = directional.value("intensity", outEnvironment.directional.intensity);
            outEnvironment.directional.color =
                JSONREAD::Vec3Or(directional.value("color", nlohmann::json::array()), outEnvironment.directional.color);
        }

        if (node.contains("directionalShadow") && node["directionalShadow"].is_object()) {
            const nlohmann::json& shadow = node["directionalShadow"];
            outEnvironment.directionalShadow.enabled = shadow.value("enabled", outEnvironment.directionalShadow.enabled);
            outEnvironment.directionalShadow.resolution = shadow.value("resolution", outEnvironment.directionalShadow.resolution);
            outEnvironment.directionalShadow.cascadeCount =
                std::clamp(
                    shadow.value(
                        "cascadeCount",
                        outEnvironment.directionalShadow.cascadeCount),
                    1u,
                    SHADOW::kMaxDirectionalShadowCascades);
            outEnvironment.directionalShadow.cascadeSplitLambda =
                std::clamp(
                    shadow.value(
                        "cascadeSplitLambda",
                        outEnvironment.directionalShadow.cascadeSplitLambda),
                    0.0f,
                    1.0f);
            outEnvironment.directionalShadow.cascadeBlend =
                std::clamp(
                    shadow.value(
                        "cascadeBlend",
                        outEnvironment.directionalShadow.cascadeBlend),
                    0.0f,
                    0.3f);
            outEnvironment.directionalShadow.nearPlane = shadow.value("nearPlane", outEnvironment.directionalShadow.nearPlane);
            outEnvironment.directionalShadow.farPlane = shadow.value("farPlane", outEnvironment.directionalShadow.farPlane);
            outEnvironment.directionalShadow.depthBias = shadow.value("depthBias", outEnvironment.directionalShadow.depthBias);
            outEnvironment.directionalShadow.normalBias = shadow.value("normalBias", outEnvironment.directionalShadow.normalBias);
            outEnvironment.directionalShadow.strength = shadow.value("strength", outEnvironment.directionalShadow.strength);
            outEnvironment.directionalShadow.pcfEnabled = shadow.value("pcfEnabled", outEnvironment.directionalShadow.pcfEnabled);
            outEnvironment.directionalShadow.pcfRadius = shadow.value("pcfRadius", outEnvironment.directionalShadow.pcfRadius);
            outEnvironment.directionalShadow.stabilize = shadow.value("stabilize", outEnvironment.directionalShadow.stabilize);
            outEnvironment.directionalShadow.showDebugTexture =
                shadow.value("showDebugTexture", outEnvironment.directionalShadow.showDebugTexture);
            outEnvironment.directionalShadow.shadowDistance =
                shadow.value("shadowDistance", outEnvironment.directionalShadow.shadowDistance);
            outEnvironment.directionalShadow.showDebugFrustum =
                shadow.value("showDebugFrustum", outEnvironment.directionalShadow.showDebugFrustum);
        }

        if (node.contains("pointLights") && node["pointLights"].is_array()) {
            outEnvironment.pointLights.clear();
            for (const nlohmann::json& pointNode : node["pointLights"]) {
                PointLight pointLight{};
                pointLight.enabled = pointNode.value("enabled", pointLight.enabled);
                pointLight.position = JSONREAD::Vec3Or(pointNode.value("position", nlohmann::json::array()), pointLight.position);
                pointLight.range = pointNode.value("range", pointLight.range);
                pointLight.distanceFalloff = RENDER3D::LIGHTING::ParseLocalLightFalloff(
                    pointNode.value("distanceFalloff", std::string("LegacyRange")));
                pointLight.color = JSONREAD::Vec3Or(pointNode.value("color", nlohmann::json::array()), pointLight.color);
                pointLight.intensity = pointNode.value("intensity", pointLight.intensity);
                pointLight.priority = pointNode.value("priority", pointLight.priority);
                outEnvironment.pointLights.push_back(pointLight);
            }
        }

        if (node.contains("spotLights") && node["spotLights"].is_array()) {
            outEnvironment.spotLights.clear();
            for (const nlohmann::json& spotNode : node["spotLights"]) {
                SpotLight spotLight{};
                spotLight.enabled =
                    spotNode.value("enabled", spotLight.enabled);
                spotLight.position = JSONREAD::Vec3Or(
                    spotNode.value("position", nlohmann::json::array()),
                    spotLight.position);
                spotLight.range =
                    spotNode.value("range", spotLight.range);
                spotLight.distanceFalloff = RENDER3D::LIGHTING::ParseLocalLightFalloff(
                    spotNode.value("distanceFalloff", std::string("LegacyRange")));
                spotLight.direction = JSONREAD::Vec3Or(
                    spotNode.value("direction", nlohmann::json::array()),
                    spotLight.direction);
                spotLight.innerConeAngleDegrees = spotNode.value(
                    "innerConeAngleDegrees",
                    spotLight.innerConeAngleDegrees);
                spotLight.outerConeAngleDegrees = spotNode.value(
                    "outerConeAngleDegrees",
                    spotLight.outerConeAngleDegrees);
                spotLight.angularFalloff = spotNode.value(
                    "angularFalloff",
                    spotLight.angularFalloff);
                spotLight.color = JSONREAD::Vec3Or(
                    spotNode.value("color", nlohmann::json::array()),
                    spotLight.color);
                spotLight.intensity =
                    spotNode.value("intensity", spotLight.intensity);
                spotLight.priority =
                    spotNode.value("priority", spotLight.priority);
                RENDER3D::LIGHTING::SanitizeSpotLight(spotLight);
                outEnvironment.spotLights.push_back(spotLight);
            }
        }

        if (node.contains("sky") && node["sky"].is_object()) {
            const nlohmann::json& sky = node["sky"];
            outEnvironment.sky.enabled = sky.value("enabled", outEnvironment.sky.enabled);
            outEnvironment.sky.mode = ParseSkyMode(sky.value("mode", nlohmann::json{}), outEnvironment.sky.mode);
            outEnvironment.sky.skyAsset = sky.value("skyAsset", outEnvironment.sky.skyAsset);
            outEnvironment.sky.panoramaCloudEnabled = sky.value(
                "panoramaCloudEnabled",
                outEnvironment.sky.panoramaCloudEnabled);
            outEnvironment.sky.panoramaCloudAsset = sky.value(
                "panoramaCloudAsset",
                outEnvironment.sky.panoramaCloudAsset);
            outEnvironment.sky.panoramaCloudScrollSpeed = ReadCloudVec2(
                sky.value(
                    "panoramaCloudScrollSpeed",
                    nlohmann::json::array()),
                outEnvironment.sky.panoramaCloudScrollSpeed,
                -1.0f,
                1.0f);
            outEnvironment.sky.panoramaCloudOpacity = ReadCloudMember(
                sky,
                "panoramaCloudOpacity",
                outEnvironment.sky.panoramaCloudOpacity,
                0.0f,
                1.0f);
            outEnvironment.sky.scale = sky.value("scale", outEnvironment.sky.scale);
            outEnvironment.sky.yaw = sky.value("yaw", outEnvironment.sky.yaw);
            outEnvironment.sky.exposure = sky.value("exposure", outEnvironment.sky.exposure);
            outEnvironment.sky.tint = JSONREAD::Vec3Or(sky.value("tint", nlohmann::json::array()), outEnvironment.sky.tint);
            outEnvironment.sky.followCamera = sky.value("followCamera", outEnvironment.sky.followCamera);
            outEnvironment.sky.zenithColor =
                JSONREAD::Vec3Or(sky.value("zenithColor", nlohmann::json::array()), outEnvironment.sky.zenithColor);
            outEnvironment.sky.horizonColor =
                JSONREAD::Vec3Or(sky.value("horizonColor", nlohmann::json::array()), outEnvironment.sky.horizonColor);
            outEnvironment.sky.groundColor =
                JSONREAD::Vec3Or(sky.value("groundColor", nlohmann::json::array()), outEnvironment.sky.groundColor);
            outEnvironment.sky.horizonPower = sky.value("horizonPower", outEnvironment.sky.horizonPower);
            outEnvironment.sky.showSunDisk = sky.value("showSunDisk", outEnvironment.sky.showSunDisk);
            outEnvironment.sky.sunDiskIntensity = sky.value("sunDiskIntensity", outEnvironment.sky.sunDiskIntensity);
            outEnvironment.sky.sunDiskSize = sky.value("sunDiskSize", outEnvironment.sky.sunDiskSize);
            outEnvironment.sky.ambientFromSky = sky.value("ambientFromSky", outEnvironment.sky.ambientFromSky);
            outEnvironment.sky.reflectionIntensity = sky.value("reflectionIntensity", outEnvironment.sky.reflectionIntensity);
            outEnvironment.sky.showDebugTexture = sky.value("showDebugTexture", outEnvironment.sky.showDebugTexture);
        }
        if ((!node.contains("sky") || !node["sky"].is_object()) &&
            node.contains("skyMode")) {
            outEnvironment.sky.mode = ParseSkyMode(
                node["skyMode"],
                outEnvironment.sky.mode);
        }

        // Atmosphere is additive to the legacy Gradient/Cubemap/Texture2D
        // payload. Missing members intentionally retain standard defaults so
        // old scenes deserialize without requiring a migration write-back.
        if (node.contains("atmosphere") && node["atmosphere"].is_object()) {
            const nlohmann::json& atmosphereNode = node["atmosphere"];
            const AtmosphereSettings defaults{};
            AtmosphereSettings& atmosphere = outEnvironment.atmosphere;
            atmosphere.enabled = JSONREAD::BooleanMemberOr(
                atmosphereNode, "enabled", atmosphere.enabled);
            atmosphere.planetRadius = ReadAtmosphereMember(
                atmosphereNode,
                "planetRadius",
                defaults.planetRadius,
                1.0f,
                100000000.0f);
            atmosphere.atmosphereHeight = ReadAtmosphereMember(
                atmosphereNode,
                "atmosphereHeight",
                defaults.atmosphereHeight,
                1.0f,
                1000000.0f);
            atmosphere.rayleighScaleHeight = ReadAtmosphereMember(
                atmosphereNode,
                "rayleighScaleHeight",
                defaults.rayleighScaleHeight,
                1.0f,
                1000000.0f);
            atmosphere.mieScaleHeight = ReadAtmosphereMember(
                atmosphereNode,
                "mieScaleHeight",
                defaults.mieScaleHeight,
                1.0f,
                1000000.0f);
            atmosphere.rayleighScattering = ReadAtmosphereVec3(
                atmosphereNode.value("rayleighScattering", nlohmann::json{}),
                defaults.rayleighScattering,
                0.0f,
                1.0f);
            atmosphere.mieScattering = ReadAtmosphereVec3(
                atmosphereNode.value("mieScattering", nlohmann::json{}),
                defaults.mieScattering,
                0.0f,
                1.0f);
            atmosphere.ozoneAbsorption = ReadAtmosphereVec3(
                atmosphereNode.value("ozoneAbsorption", nlohmann::json{}),
                defaults.ozoneAbsorption,
                0.0f,
                1.0f);
            atmosphere.ozoneCenterHeight = ReadAtmosphereMember(
                atmosphereNode,
                "ozoneCenterHeight",
                defaults.ozoneCenterHeight,
                0.0f,
                1000000.0f);
            atmosphere.ozoneWidth = ReadAtmosphereMember(
                atmosphereNode,
                "ozoneWidth",
                defaults.ozoneWidth,
                1.0f,
                1000000.0f);
            atmosphere.mieAnisotropy = ReadAtmosphereMember(
                atmosphereNode,
                "mieAnisotropy",
                defaults.mieAnisotropy,
                -0.95f,
                0.95f);
            atmosphere.sunIlluminance = ReadAtmosphereMember(
                atmosphereNode,
                "sunIlluminance",
                defaults.sunIlluminance,
                0.0f,
                16.0f);
            atmosphere.sunExposure = ReadAtmosphereMember(
                atmosphereNode,
                "sunExposure",
                defaults.sunExposure,
                -16.0f,
                16.0f);
            atmosphere.groundAlbedo = ReadAtmosphereMember(
                atmosphereNode,
                "groundAlbedo",
                defaults.groundAlbedo,
                0.0f,
                1.0f);
            atmosphere.aerialPerspectiveStrength = ReadAtmosphereMember(
                atmosphereNode,
                "aerialPerspectiveStrength",
                defaults.aerialPerspectiveStrength,
                0.0f,
                8.0f);
            atmosphere.sunAngularRadius = ReadAtmosphereMember(
                atmosphereNode,
                "sunAngularRadius",
                defaults.sunAngularRadius,
                0.0001f,
                0.25f);
            atmosphere.transmittanceLutWidth = ReadAtmosphereUInt(
                atmosphereNode,
                "transmittanceLutWidth",
                defaults.transmittanceLutWidth,
                1u,
                4096u);
            atmosphere.transmittanceLutHeight = ReadAtmosphereUInt(
                atmosphereNode,
                "transmittanceLutHeight",
                defaults.transmittanceLutHeight,
                1u,
                4096u);
            atmosphere.multiScatteringLutSize = ReadAtmosphereUInt(
                atmosphereNode,
                "multiScatteringLutSize",
                defaults.multiScatteringLutSize,
                1u,
                4096u);
            atmosphere.skyViewLutWidth = ReadAtmosphereUInt(
                atmosphereNode,
                "skyViewLutWidth",
                defaults.skyViewLutWidth,
                1u,
                4096u);
            atmosphere.skyViewLutHeight = ReadAtmosphereUInt(
                atmosphereNode,
                "skyViewLutHeight",
                defaults.skyViewLutHeight,
                1u,
                4096u);
            atmosphere.preferLut = JSONREAD::BooleanMemberOr(
                atmosphereNode,
                "preferLut",
                defaults.preferLut);
        }

        if (node.contains("sunGlare") && node["sunGlare"].is_object()) {
            const nlohmann::json& glareNode = node["sunGlare"];
            const SunGlareSettings defaults{};
            SunGlareSettings& glare = outEnvironment.sunGlare;
            glare.enabled = JSONREAD::BooleanMemberOr(
                glareNode, "enabled", glare.enabled);
            glare.intensity = ReadAtmosphereMember(
                glareNode, "intensity", defaults.intensity, 0.0f, 8.0f);
            glare.haloRadius = ReadAtmosphereMember(
                glareNode, "haloRadius", defaults.haloRadius, 0.0f, 2.0f);
            glare.haloFalloff = ReadAtmosphereMember(
                glareNode, "haloFalloff", defaults.haloFalloff, 0.1f, 16.0f);
            glare.streakIntensity = ReadAtmosphereMember(
                glareNode, "streakIntensity", defaults.streakIntensity, 0.0f, 8.0f);
            glare.streakLength = ReadAtmosphereMember(
                glareNode, "streakLength", defaults.streakLength, 0.0f, 2.0f);
            glare.occlusionRadius = ReadAtmosphereMember(
                glareNode, "occlusionRadius", defaults.occlusionRadius, 0.0f, 0.25f);
            glare.edgeFade = ReadAtmosphereMember(
                glareNode, "edgeFade", defaults.edgeFade, 0.0f, 1.0f);
            glare.maxRadiance = ReadAtmosphereMember(
                glareNode, "maxRadiance", defaults.maxRadiance, 0.0f, 64.0f);
            glare.shaftIntensity = ReadAtmosphereMember(
                glareNode,
                "shaftIntensity",
                defaults.shaftIntensity,
                0.0f,
                8.0f);
            glare.shaftLength = ReadAtmosphereMember(
                glareNode,
                "shaftLength",
                defaults.shaftLength,
                0.01f,
                2.0f);
            glare.shaftDecay = ReadAtmosphereMember(
                glareNode,
                "shaftDecay",
                defaults.shaftDecay,
                0.5f,
                0.999f);
        }

        if (node.contains("clouds") && node["clouds"].is_object()) {
            const nlohmann::json& cloudNode = node["clouds"];
            StylizedCloudSettings& clouds = outEnvironment.clouds;
            const StylizedCloudSettings defaults{};
            clouds.enabled = JSONREAD::BooleanMemberOr(
                cloudNode, "enabled", defaults.enabled);
            clouds.coverage = ReadCloudMember(
                cloudNode, "coverage", defaults.coverage, 0.0f, 1.0f);
            clouds.density = ReadCloudMember(
                cloudNode, "density", defaults.density, 0.0f, 8.0f);
            clouds.altitude = ReadCloudMember(
                cloudNode, "altitude", defaults.altitude, 0.0f, 100000.0f);
            clouds.thickness = ReadCloudMember(
                cloudNode, "thickness", defaults.thickness, 0.1f, 100000.0f);
            clouds.scale = ReadCloudMember(
                cloudNode, "scale", defaults.scale, 0.1f, 100000.0f);
            clouds.windDirection = ReadCloudVec2(
                cloudNode.value("windDirection", nlohmann::json{}),
                defaults.windDirection,
                kCloudDirectionMin,
                kCloudDirectionMax);
            clouds.windSpeed = ReadCloudMember(
                cloudNode, "windSpeed", defaults.windSpeed, 0.0f, 1000.0f);
            clouds.shapeDetail = ReadCloudMember(
                cloudNode, "shapeDetail", defaults.shapeDetail, 0.0f, 1.0f);
            clouds.softness = ReadCloudMember(
                cloudNode, "softness", defaults.softness, 0.0f, 1.0f);
            clouds.lightAbsorption = ReadCloudMember(
                cloudNode, "lightAbsorption", defaults.lightAbsorption, 0.0f, 8.0f);
            clouds.silverLining = ReadCloudMember(
                cloudNode, "silverLining", defaults.silverLining, 0.0f, 4.0f);
            clouds.horizonFade = ReadCloudMember(
                cloudNode, "horizonFade", defaults.horizonFade, 0.0f, 1.0f);
            clouds.brightColor = ReadCloudVec3(
                cloudNode.value("brightColor", nlohmann::json{}),
                defaults.brightColor,
                kCloudColorMin,
                kCloudColorMax);
            clouds.shadowColor = ReadCloudVec3(
                cloudNode.value("shadowColor", nlohmann::json{}),
                defaults.shadowColor,
                kCloudColorMin,
                kCloudColorMax);
        }

        if (node.contains("reflectionProbe") && node["reflectionProbe"].is_object()) {
            const nlohmann::json& probe = node["reflectionProbe"];
            outEnvironment.reflectionProbe.enabled = probe.value("enabled", outEnvironment.reflectionProbe.enabled);
            outEnvironment.reflectionProbe.sourceCubemapAsset =
                probe.value("sourceCubemapAsset", outEnvironment.reflectionProbe.sourceCubemapAsset);
            outEnvironment.reflectionProbe.position =
                JSONREAD::Vec3Or(probe.value("position", nlohmann::json::array()), outEnvironment.reflectionProbe.position);
            outEnvironment.reflectionProbe.radius = probe.value("radius", outEnvironment.reflectionProbe.radius);
            outEnvironment.reflectionProbe.intensity = probe.value("intensity", outEnvironment.reflectionProbe.intensity);
            outEnvironment.reflectionProbe.influenceShape = ParseReflectionProbeInfluenceShape(
                probe.value("influenceShape", nlohmann::json{}), outEnvironment.reflectionProbe.influenceShape);
            outEnvironment.reflectionProbe.projectionShape = ParseReflectionProbeProjectionShape(
                probe.value("projectionShape", nlohmann::json{}), outEnvironment.reflectionProbe.projectionShape);

            const MATH::Vec3 radiusBoxSize = ReflectionProbeRadiusBoxSize(outEnvironment.reflectionProbe.radius);
            const bool hasInfluenceBoxCenter = probe.contains("influenceBoxCenter");
            const bool hasInfluenceBoxSize = probe.contains("influenceBoxSize");
            const bool hasProjectionBoxCenter = probe.contains("projectionBoxCenter");
            const bool hasProjectionBoxSize = probe.contains("projectionBoxSize");

            outEnvironment.reflectionProbe.influenceBoxCenter = JSONREAD::Vec3Or(
                probe.value("influenceBoxCenter", nlohmann::json::array()),
                hasInfluenceBoxCenter ? outEnvironment.reflectionProbe.influenceBoxCenter : outEnvironment.reflectionProbe.position);
            outEnvironment.reflectionProbe.influenceBoxSize =
                JSONREAD::Vec3Or(probe.value("influenceBoxSize", nlohmann::json::array()),
                                 hasInfluenceBoxSize ? outEnvironment.reflectionProbe.influenceBoxSize : radiusBoxSize);
            outEnvironment.reflectionProbe.projectionBoxCenter = JSONREAD::Vec3Or(
                probe.value("projectionBoxCenter", nlohmann::json::array()),
                hasProjectionBoxCenter ? outEnvironment.reflectionProbe.projectionBoxCenter : outEnvironment.reflectionProbe.position);
            outEnvironment.reflectionProbe.projectionBoxSize =
                JSONREAD::Vec3Or(probe.value("projectionBoxSize", nlohmann::json::array()),
                                 hasProjectionBoxSize ? outEnvironment.reflectionProbe.projectionBoxSize : radiusBoxSize);
            outEnvironment.reflectionProbe.blendDistance = probe.value("blendDistance", outEnvironment.reflectionProbe.blendDistance);
            outEnvironment.reflectionProbe.priority = probe.value("priority", outEnvironment.reflectionProbe.priority);
        }

        if (node.contains("ambientOcclusion") && node["ambientOcclusion"].is_object()) {
            const nlohmann::json& ao = node["ambientOcclusion"];
            const bool hasSsaoMode = ao.contains("mode");
            const bool legacyEnabled = ao.value("enabled", outEnvironment.ambientOcclusion.enabled);
            if (hasSsaoMode) {
                outEnvironment.ambientOcclusion.mode =
                    ParseSsaoMode(ao.value("mode", nlohmann::json{}), outEnvironment.ambientOcclusion.mode);
                outEnvironment.ambientOcclusion.enabled = outEnvironment.ambientOcclusion.mode != SsaoMode::Off;
            } else {
                outEnvironment.ambientOcclusion.enabled = legacyEnabled;
                outEnvironment.ambientOcclusion.mode = legacyEnabled ? outEnvironment.ambientOcclusion.mode : SsaoMode::Off;
            }
            outEnvironment.ambientOcclusion.radius = ao.value("radius", outEnvironment.ambientOcclusion.radius);
            outEnvironment.ambientOcclusion.bias = ao.value("bias", outEnvironment.ambientOcclusion.bias);
            outEnvironment.ambientOcclusion.strength = ao.value("strength", outEnvironment.ambientOcclusion.strength);
            outEnvironment.ambientOcclusion.power = ao.value("power", outEnvironment.ambientOcclusion.power);
            outEnvironment.ambientOcclusion.diffuseStrength = ao.value("diffuseStrength", outEnvironment.ambientOcclusion.diffuseStrength);
            outEnvironment.ambientOcclusion.specularStrength =
                ao.value("specularStrength", outEnvironment.ambientOcclusion.specularStrength);
            outEnvironment.ambientOcclusion.sampleCount = ao.value("sampleCount", outEnvironment.ambientOcclusion.sampleCount);
            outEnvironment.ambientOcclusion.blurIterations = ao.value("blurIterations", outEnvironment.ambientOcclusion.blurIterations);
            outEnvironment.ambientOcclusion.sampleCount = std::clamp<uint32_t>(outEnvironment.ambientOcclusion.sampleCount, 8u, 32u);
            outEnvironment.ambientOcclusion.blurIterations = std::clamp<uint32_t>(outEnvironment.ambientOcclusion.blurIterations, 0u, 4u);
        }

        if (node.contains("surfaceLightmaps") &&
            node["surfaceLightmaps"].is_object()) {
            const nlohmann::json& lightmaps =
                node["surfaceLightmaps"];
            outEnvironment.surfaceLightmaps.enabled =
                lightmaps.value(
                    "enabled",
                    outEnvironment.surfaceLightmaps.enabled);
        }

        if (node.contains("screenSpaceReflections") &&
            node["screenSpaceReflections"].is_object()) {
            const nlohmann::json& ssr = node["screenSpaceReflections"];
            ScreenSpaceReflectionSettings& settings =
                outEnvironment.screenSpaceReflections;
            settings.enabled = ssr.value("enabled", settings.enabled);
            settings.quality = ParseSsrQuality(
                ssr.value("quality", nlohmann::json{}),
                settings.quality);
            settings.intensity = std::clamp(
                ssr.value("intensity", settings.intensity),
                0.0f,
                2.0f);
            settings.maxDistance = std::clamp(
                ssr.value("maxDistance", settings.maxDistance),
                1.0f,
                200.0f);
            settings.thickness = std::clamp(
                ssr.value("thickness", settings.thickness),
                0.0001f,
                0.05f);
            settings.maxRoughness = std::clamp(
                ssr.value("maxRoughness", settings.maxRoughness),
                0.05f,
                1.0f);
            settings.edgeFade = std::clamp(
                ssr.value("edgeFade", settings.edgeFade),
                0.01f,
                0.35f);
            settings.stride = std::clamp(
                ssr.value("stride", settings.stride),
                0.02f,
                2.0f);
        }

        if (node.contains("bloom") && node["bloom"].is_object()) {
            const nlohmann::json& bloom = node["bloom"];
            outEnvironment.bloom.enabled = bloom.value("enabled", outEnvironment.bloom.enabled);
            outEnvironment.bloom.threshold = bloom.value("threshold", outEnvironment.bloom.threshold);
            outEnvironment.bloom.intensity = bloom.value("intensity", outEnvironment.bloom.intensity);
            outEnvironment.bloom.radius = bloom.value("radius", outEnvironment.bloom.radius);
            outEnvironment.bloom.downsampleCount = bloom.value("downsampleCount", outEnvironment.bloom.downsampleCount);
        }

        if (node.contains("fog") && node["fog"].is_object()) {
            const nlohmann::json& fog = node["fog"];
            outEnvironment.fog.enabled = fog.value("enabled", outEnvironment.fog.enabled);
            outEnvironment.fog.volumetric = fog.value("volumetric", outEnvironment.fog.volumetric);
            outEnvironment.fog.color = JSONREAD::Vec3Or(fog.value("color", nlohmann::json::array()), outEnvironment.fog.color);
            outEnvironment.fog.density = fog.value("density", outEnvironment.fog.density);
            outEnvironment.fog.startDistance = fog.value("startDistance", outEnvironment.fog.startDistance);
            outEnvironment.fog.endDistance = fog.value("endDistance", outEnvironment.fog.endDistance);
            outEnvironment.fog.heightFalloff = fog.value("heightFalloff", outEnvironment.fog.heightFalloff);
            outEnvironment.fog.baseHeight = fog.value("baseHeight", outEnvironment.fog.baseHeight);
            outEnvironment.fog.anisotropy = fog.value("anisotropy", outEnvironment.fog.anisotropy);
            outEnvironment.fog.temporalWeight = fog.value("temporalWeight", outEnvironment.fog.temporalWeight);
            outEnvironment.fog.scatteringIntensity = fog.value(
                "scatteringIntensity",
                outEnvironment.fog.scatteringIntensity);
            outEnvironment.fog.ambientScatteringStrength = fog.value(
                "ambientScatteringStrength",
                outEnvironment.fog.ambientScatteringStrength);
            outEnvironment.fog.sunShaftStrength = fog.value(
                "sunShaftStrength",
                outEnvironment.fog.sunShaftStrength);
            outEnvironment.fog.useSkyHorizonColor = fog.value("useSkyHorizonColor", outEnvironment.fog.useSkyHorizonColor);
        }

        if (node.contains("toneMapping") && node["toneMapping"].is_object()) {
            const nlohmann::json& toneMapping = node["toneMapping"];
            outEnvironment.toneMapping.enabled = toneMapping.value("enabled", outEnvironment.toneMapping.enabled);
            if (toneMapping.contains("exposureMode")) {
                outEnvironment.toneMapping.exposureMode =
                    ParseExposureMode(
                        toneMapping["exposureMode"],
                        outEnvironment.toneMapping.exposureMode);
            }
            outEnvironment.toneMapping.exposure = toneMapping.value("exposure", outEnvironment.toneMapping.exposure);
            if (toneMapping.contains("automaticExposure") &&
                toneMapping["automaticExposure"].is_object()) {
                const nlohmann::json& automaticExposure =
                    toneMapping["automaticExposure"];
                auto& settings =
                    outEnvironment.toneMapping.automaticExposure;
                settings.targetLuminance = automaticExposure.value(
                    "targetLuminance",
                    settings.targetLuminance);
                settings.compensationEv = automaticExposure.value(
                    "compensationEv",
                    settings.compensationEv);
                settings.minExposureEv = automaticExposure.value(
                    "minExposureEv",
                    settings.minExposureEv);
                settings.maxExposureEv = automaticExposure.value(
                    "maxExposureEv",
                    settings.maxExposureEv);
                settings.speedUp = automaticExposure.value(
                    "speedUp",
                    settings.speedUp);
                settings.speedDown = automaticExposure.value(
                    "speedDown",
                    settings.speedDown);
            }
            outEnvironment.toneMapping.whiteBalanceTemperature =
                toneMapping.value(
                    "whiteBalanceTemperature",
                    outEnvironment.toneMapping.whiteBalanceTemperature);
            outEnvironment.toneMapping.whiteBalanceTint =
                toneMapping.value(
                    "whiteBalanceTint",
                    outEnvironment.toneMapping.whiteBalanceTint);
            outEnvironment.toneMapping.contrast = toneMapping.value(
                "contrast",
                outEnvironment.toneMapping.contrast);
            outEnvironment.toneMapping.saturation = toneMapping.value(
                "saturation",
                outEnvironment.toneMapping.saturation);
            outEnvironment.toneMapping.gamma = toneMapping.value("gamma", outEnvironment.toneMapping.gamma);
            outEnvironment.toneMapping.mode = toneMapping.value("mode", outEnvironment.toneMapping.mode);
        }

        // 旧 Phong の specularIntensity/specularPower は PBR では使わない。
        // 既存 Scene に残るキーは他の未知キーと同様に読み飛ばす。
        outEnvironment.showLightDebug = node.value("showLightDebug", outEnvironment.showLightDebug);
        outEnvironment.showSkyDebugInfo = node.value("showSkyDebugInfo", outEnvironment.showSkyDebugInfo);

        if (node.contains("post") && node["post"].is_object()) {
            const nlohmann::json& post = node["post"];
            outEnvironment.post.enabled = post.value("enabled", outEnvironment.post.enabled);
            outEnvironment.post.globalPostProfileId = post.value("globalPostProfileId", outEnvironment.post.globalPostProfileId);
            bool hasParamValues = false;
            if (post.contains("paramValues") && post["paramValues"].is_array()) {
                const nlohmann::json& paramValues = post["paramValues"];
                const size_t count = std::min<size_t>(paramValues.size(), std::size(outEnvironment.post.paramValues));
                for (size_t i = 0; i < count; ++i) {
                    outEnvironment.post.paramValues[i] = DeserializeFloat4(paramValues[i], outEnvironment.post.paramValues[i]);
                }
                hasParamValues = true;
            } else if (post.contains("userOverrides") && post["userOverrides"].is_array()) {
                const nlohmann::json& overrides = post["userOverrides"];
                const size_t count = std::min<size_t>(overrides.size(), std::size(outEnvironment.post.paramValues));
                for (size_t i = 0; i < count; ++i) {
                    outEnvironment.post.paramValues[i] = DeserializeFloat4(overrides[i], outEnvironment.post.paramValues[i]);
                }
                hasParamValues = true;
            }
            outEnvironment.post.valuesInitialized = post.value("valuesInitialized", hasParamValues);
        }
    }

} // namespace HIKARI::SCENE::SERIALIZATION
