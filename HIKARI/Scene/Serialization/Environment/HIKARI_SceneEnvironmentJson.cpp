#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
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
            case SkyMode::Gradient:
            default:
                return "Gradient";
            }
        }

        SkyMode ParseSkyMode(const nlohmann::json& node, SkyMode fallback) {

            if (node.is_number_integer()) {
                const int value = node.get<int>();
                if (value >= static_cast<int>(SkyMode::None) && value <= static_cast<int>(SkyMode::Texture2D)) {
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

        MATH::Vec3 ReflectionProbeRadiusBoxSize(float radius) {
            const float diameter = std::max(0.01f, radius * 2.0f);
            return { diameter, diameter, diameter };
        }

    } // namespace

    void SerializeSceneEnvironmentJson(const SceneEnvironment& environment, nlohmann::json& out) {

        out = nlohmann::json::object();
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
        out["directionalShadow"]["orthoSize"] = environment.directionalShadow.orthoSize;
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
                                           { "color", JsonMath::ToJsonArray(pointLight.color) },
                                           { "intensity", pointLight.intensity } });
        }

        out["sky"]["enabled"] = environment.sky.enabled;
        out["sky"]["mode"] = ToString(environment.sky.mode);
        out["sky"]["skyAsset"] = environment.sky.skyAsset;
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
        out["fog"]["anisotropy"] = environment.fog.anisotropy;
        out["fog"]["temporalWeight"] = environment.fog.temporalWeight;
        out["fog"]["useSkyHorizonColor"] = environment.fog.useSkyHorizonColor;

        out["toneMapping"]["enabled"] = environment.toneMapping.enabled;
        out["toneMapping"]["exposure"] = environment.toneMapping.exposure;
        out["toneMapping"]["gamma"] = environment.toneMapping.gamma;
        out["toneMapping"]["mode"] = environment.toneMapping.mode;

        out["specularIntensity"] = environment.specularIntensity;
        out["specularPower"] = environment.specularPower;
        out["showLightDebug"] = environment.showLightDebug;
        out["showPointLightMarkers"] = environment.showPointLightMarkers;
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
            outEnvironment.directionalShadow.orthoSize = shadow.value("orthoSize", outEnvironment.directionalShadow.orthoSize);
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
                pointLight.color = JSONREAD::Vec3Or(pointNode.value("color", nlohmann::json::array()), pointLight.color);
                pointLight.intensity = pointNode.value("intensity", pointLight.intensity);
                outEnvironment.pointLights.push_back(pointLight);
            }
        }

        if (node.contains("sky")) {
            const nlohmann::json& sky = node["sky"];
            outEnvironment.sky.enabled = sky.value("enabled", outEnvironment.sky.enabled);
            outEnvironment.sky.mode = ParseSkyMode(sky.value("mode", nlohmann::json{}), outEnvironment.sky.mode);
            outEnvironment.sky.skyAsset = sky.value("skyAsset", outEnvironment.sky.skyAsset);
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
            outEnvironment.fog.anisotropy = fog.value("anisotropy", outEnvironment.fog.anisotropy);
            outEnvironment.fog.temporalWeight = fog.value("temporalWeight", outEnvironment.fog.temporalWeight);
            outEnvironment.fog.useSkyHorizonColor = fog.value("useSkyHorizonColor", outEnvironment.fog.useSkyHorizonColor);
        }

        if (node.contains("toneMapping") && node["toneMapping"].is_object()) {
            const nlohmann::json& toneMapping = node["toneMapping"];
            outEnvironment.toneMapping.enabled = toneMapping.value("enabled", outEnvironment.toneMapping.enabled);
            outEnvironment.toneMapping.exposure = toneMapping.value("exposure", outEnvironment.toneMapping.exposure);
            outEnvironment.toneMapping.gamma = toneMapping.value("gamma", outEnvironment.toneMapping.gamma);
            outEnvironment.toneMapping.mode = toneMapping.value("mode", outEnvironment.toneMapping.mode);
        }

        outEnvironment.specularIntensity = node.value("specularIntensity", outEnvironment.specularIntensity);
        outEnvironment.specularPower = node.value("specularPower", outEnvironment.specularPower);
        outEnvironment.showLightDebug = node.value("showLightDebug", outEnvironment.showLightDebug);
        outEnvironment.showPointLightMarkers = node.value("showPointLightMarkers", outEnvironment.showPointLightMarkers);
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
