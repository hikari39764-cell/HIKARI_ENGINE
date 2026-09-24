#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Lighting/Internal/HIKARI_LightAuthoringInternal.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace JsonMath = SERIALIZATION::JSON::MATH;

namespace {

bool HasLightComponent(const SceneDocument& document) {
    return std::any_of(document.objects.begin(), document.objects.end(), [](const SceneObjectData& object) {
        return FindLightingComponent(object, kLightComponentType) != nullptr;
    });
}

RENDER3D::LIGHTING::SceneLightType ReadLightType(const nlohmann::json& properties) {
    const std::string value = properties.value("type", std::string("Point"));
    if (value == "Directional") {
        return RENDER3D::LIGHTING::SceneLightType::Directional;
    }
    if (value == "Spot") {
        return RENDER3D::LIGHTING::SceneLightType::Spot;
    }
    return RENDER3D::LIGHTING::SceneLightType::Point;
}

MATH::Vec3 ForwardFromEulerDegrees(const MATH::Vec3& eulerDegrees) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const MATH::Quat rotation =
        MATH::Quat::FromEulerXYZ(eulerDegrees.x * kDegToRad, eulerDegrees.y * kDegToRad, eulerDegrees.z * kDegToRad);
    const MATH::Mat4 matrix = MATH::Mat4::Rotate(rotation);
    return MATH::Normalize({matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]});
}

MATH::Vec3 EulerDegreesFromForward(const MATH::Vec3& direction) {
    const MATH::Vec3 forward = MATH::Normalize(direction);
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    return {std::asin(std::clamp(-forward.y, -1.0f, 1.0f)) * kRadToDeg, std::atan2(forward.x, forward.z) * kRadToDeg,
            0.0f};
}

LightAuthoring ReadLight(const SceneObjectData& object, const SceneComponentData& component) {
    const nlohmann::json& properties = component.properties;
    LightAuthoring light{};
    light.objectId = object.id;
    light.name = object.name;
    light.objectEnabled = object.enabled;
    light.position = object.transform.position;
    light.rotationEulerDeg = object.transform.rotationEulerDeg;
    light.direction = ForwardFromEulerDegrees(light.rotationEulerDeg);
    light.type = ReadLightType(properties);
    light.distanceFalloff =
        RENDER3D::LIGHTING::ParseLocalLightFalloff(properties.value("distanceFalloff", std::string("LegacyRange")));
    light.enabled = object.enabled && properties.value("enabled", true);
    light.color = JSONREAD::Vec3Or(properties.value("color", nlohmann::json::array()), {1.0f, 1.0f, 1.0f});
    light.intensity = std::clamp(properties.value("intensity", 1.0f), 0.0f, 100.0f);
    light.range = (std::max)(properties.value("range", 5.0f), 0.05f);
    light.castsShadows = properties.value("castsShadows", false);
    light.sourceRadius = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(properties.value("sourceRadius", 0.0f));
    light.innerConeAngleDegrees = std::clamp(properties.value("innerConeAngleDegrees", 25.0f), 0.1f, 89.0f);
    light.outerConeAngleDegrees =
        std::clamp(properties.value("outerConeAngleDegrees", 35.0f), light.innerConeAngleDegrees, 89.0f);
    light.angularFalloff = std::clamp(properties.value("angularFalloff", 1.0f), 0.05f, 8.0f);
    light.priority = properties.value("priority", 0);
    return light;
}

} // namespace

MATH::Vec3 LightDirectionFromEulerDegrees(const MATH::Vec3& eulerDegrees) noexcept {
    return ForwardFromEulerDegrees(eulerDegrees);
}

MATH::Vec3 DirectionalLightEulerDegreesFromDirection(const MATH::Vec3& direction) noexcept {
    MATH::Vec3 canonical = EulerDegreesFromForward(direction);
    canonical.x = std::clamp(canonical.x, -89.9f, 89.9f);
    canonical.y = std::remainder(canonical.y, 360.0f);
    canonical.z = 0.0f;
    return canonical;
}

MATH::Vec3 CanonicalDirectionalLightEulerDegrees(const MATH::Vec3& eulerDegrees) noexcept {
    return DirectionalLightEulerDegreesFromDirection(LightDirectionFromEulerDegrees(eulerDegrees));
}

std::vector<LightAuthoring> CollectLights(const SceneDocument& document) { return CollectLights(document.objects); }

std::vector<LightAuthoring> CollectLights(std::span<const SceneObjectData> objects) {
    std::vector<LightAuthoring> lights{};
    for (const SceneObjectData& object : objects) {
        const SceneComponentData* component = FindLightingComponent(object, kLightComponentType);
        if (component != nullptr && component->properties.is_object()) {
            lights.push_back(ReadLight(object, *component));
        }
    }
    std::stable_sort(lights.begin(), lights.end(), [](const LightAuthoring& lhs, const LightAuthoring& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.objectId.value < rhs.objectId.value;
    });
    return lights;
}

SceneObjectId AddLight(SceneDocument& document, const LightAuthoring& light) {
    SceneObjectData object{};
    object.id = light.objectId.value != 0u ? light.objectId : AllocateSceneObjectId(document);
    object.name = light.name.empty() ? "Point Light" : light.name;
    object.enabled = light.objectEnabled;
    object.transform.position = light.position;
    object.transform.rotationEulerDeg = light.rotationEulerDeg;
    const char* typeName = "Point";
    if (light.type == RENDER3D::LIGHTING::SceneLightType::Directional) {
        typeName = "Directional";
    } else if (light.type == RENDER3D::LIGHTING::SceneLightType::Spot) {
        typeName = "Spot";
    }
    object.components.push_back(
        SceneComponentData{kLightComponentType,
                           {{"enabled", light.enabled},
                            {"type", typeName},
                            {"color", JsonMath::ToJsonArray(light.color)},
                            {"intensity", light.intensity},
                            {"range", light.range},
                            {"sourceRadius", RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(light.sourceRadius)},
                            {"castsShadows", light.castsShadows},
                            {"distanceFalloff", RENDER3D::LIGHTING::ToString(light.distanceFalloff)},
                            {"innerConeAngleDegrees", light.innerConeAngleDegrees},
                            {"outerConeAngleDegrees", light.outerConeAngleDegrees},
                            {"angularFalloff", light.angularFalloff},
                            {"priority", light.priority}}});
    NormalizeSceneComponentIds(object);
    const SceneObjectId id = object.id;
    document.objects.push_back(std::move(object));
    return id;
}

namespace INTERNAL {

void MigrateLegacyLights(SceneDocument& document, uint32_t sourceVersion) {
    if (sourceVersion >= 6u || HasLightComponent(document)) {
        return;
    }

    const DirectionalLight& legacyDirectional = document.environment.directional;
    LightAuthoring directional{};
    directional.name = "Directional Light";
    directional.position = {0.0f, 2.0f, 0.0f};
    directional.rotationEulerDeg = EulerDegreesFromForward(legacyDirectional.direction);
    directional.type = RENDER3D::LIGHTING::SceneLightType::Directional;
    directional.enabled = legacyDirectional.enabled;
    directional.color = legacyDirectional.color;
    directional.intensity = legacyDirectional.intensity;
    directional.priority = 100;
    (void)AddLight(document, directional);

    for (size_t index = 0u; index < document.environment.pointLights.size(); ++index) {
        const PointLight& legacy = document.environment.pointLights[index];
        LightAuthoring point{};
        point.name = "Point Light " + std::to_string(index);
        point.position = legacy.position;
        point.type = RENDER3D::LIGHTING::SceneLightType::Point;
        point.enabled = legacy.enabled;
        point.color = legacy.color;
        point.intensity = legacy.intensity;
        point.range = legacy.range;
        point.distanceFalloff = legacy.distanceFalloff;
        point.sourceRadius = legacy.sourceRadius;
        point.castsShadows = false;
        point.priority = legacy.priority;
        (void)AddLight(document, point);
    }

    for (size_t index = 0u; index < document.environment.spotLights.size(); ++index) {
        const SpotLight& legacy = document.environment.spotLights[index];
        LightAuthoring spot{};
        spot.name = "Spot Light " + std::to_string(index);
        spot.position = legacy.position;
        spot.rotationEulerDeg = EulerDegreesFromForward(legacy.direction);
        spot.type = RENDER3D::LIGHTING::SceneLightType::Spot;
        spot.enabled = legacy.enabled;
        spot.color = legacy.color;
        spot.intensity = legacy.intensity;
        spot.range = legacy.range;
        spot.distanceFalloff = legacy.distanceFalloff;
        spot.sourceRadius = legacy.sourceRadius;
        spot.castsShadows = false;
        spot.innerConeAngleDegrees = legacy.innerConeAngleDegrees;
        spot.outerConeAngleDegrees = legacy.outerConeAngleDegrees;
        spot.angularFalloff = legacy.angularFalloff;
        spot.priority = legacy.priority;
        (void)AddLight(document, spot);
    }
}

} // namespace INTERNAL

} // namespace HIKARI::LIGHTING_AUTHORING
