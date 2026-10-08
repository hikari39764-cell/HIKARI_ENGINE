#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace JsonMath = SERIALIZATION::JSON::MATH;

namespace {

RENDER3D::LIGHTING::SceneLightType ReadLightType(const nlohmann::json& properties) {
    return properties.value("type", std::string("Point")) == "Spot" ? RENDER3D::LIGHTING::SceneLightType::Spot
                                                                    : RENDER3D::LIGHTING::SceneLightType::Point;
}

MATH::Vec3 ForwardFromEulerDegrees(const MATH::Vec3& eulerDegrees) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const MATH::Quat rotation =
        MATH::Quat::FromEulerXYZ(eulerDegrees.x * kDegToRad, eulerDegrees.y * kDegToRad, eulerDegrees.z * kDegToRad);
    const MATH::Mat4 matrix = MATH::Mat4::Rotate(rotation);
    return MATH::Normalize({matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]});
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
    light.enabled = object.enabled && properties.value("enabled", true);
    light.color = JSONREAD::Vec3Or(properties.value("color", nlohmann::json::array()), {1.0f, 1.0f, 1.0f});
    light.intensity = std::clamp(properties.value("intensity", 800.0f), 0.0f, 1000000.0f);
    light.range = (std::max)(properties.value("range", 10.0f), 0.05f);
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
    const char* typeName = light.type == RENDER3D::LIGHTING::SceneLightType::Spot ? "Spot" : "Point";
    object.components.push_back(
        SceneComponentData{kLightComponentType,
                           {{"enabled", light.enabled},
                            {"type", typeName},
                            {"color", JsonMath::ToJsonArray(light.color)},
                            {"intensity", light.intensity},
                            {"range", light.range},
                            {"sourceRadius", RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(light.sourceRadius)},
                            {"castsShadows", light.castsShadows},
                            {"innerConeAngleDegrees", light.innerConeAngleDegrees},
                            {"outerConeAngleDegrees", light.outerConeAngleDegrees},
                            {"angularFalloff", light.angularFalloff},
                            {"priority", light.priority}}});
    NormalizeSceneComponentIds(object);
    const SceneObjectId id = object.id;
    document.objects.push_back(std::move(object));
    return id;
}

} // namespace HIKARI::LIGHTING_AUTHORING
