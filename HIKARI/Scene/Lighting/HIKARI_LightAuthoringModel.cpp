#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <utility>

#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace {

MATH::Mat4 RotationFromEulerDegrees(const MATH::Vec3& eulerDegrees) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    return MATH::Mat4::Rotate(
        MATH::Quat::FromEulerXYZ(eulerDegrees.x * kDegToRad, eulerDegrees.y * kDegToRad, eulerDegrees.z * kDegToRad));
}

LightAuthoring ReadLight(const SceneObjectData& object, const SceneComponentData& component) {
    LightAuthoring light{};
    light.objectId = object.id;
    light.name = object.name;
    light.objectEnabled = object.enabled;
    light.position = object.transform.position;
    light.rotationEulerDeg = object.transform.rotationEulerDeg;
    const MATH::Mat4 rotation = RotationFromEulerDegrees(light.rotationEulerDeg);
    light.direction = MATH::Normalize({rotation.m[2][0], rotation.m[2][1], rotation.m[2][2]});
    light.tangent = MATH::Normalize({rotation.m[0][0], rotation.m[0][1], rotation.m[0][2]});
    light.settings = ReadLightSettings(component.properties);
    light.settings.enabled = object.enabled && light.settings.enabled;
    return light;
}

} // namespace

std::vector<LightAuthoring> CollectLights(const SceneDocument& document) { return CollectLights(document.objects); }

std::vector<LightAuthoring> CollectLights(std::span<const SceneObjectData> objects) {
    std::vector<LightAuthoring> lights{};
    for (const SceneObjectData& object : objects) {
        const SceneComponentData* component = object.FindComponent(kLightComponentType);
        if (component != nullptr && component->properties.is_object()) {
            lights.push_back(ReadLight(object, *component));
        }
    }
    std::stable_sort(lights.begin(), lights.end(), [](const LightAuthoring& lhs, const LightAuthoring& rhs) {
        if (lhs.settings.priority != rhs.settings.priority) {
            return lhs.settings.priority > rhs.settings.priority;
        }
        return lhs.objectId.value < rhs.objectId.value;
    });
    return lights;
}

SceneObjectId AddLight(SceneDocument& document, const LightAuthoring& light) {
    SceneObjectData object{};
    object.id = light.objectId.value != 0u ? light.objectId : AllocateSceneObjectId(document);
    object.name = light.name.empty() ? "Light" : light.name;
    object.enabled = light.objectEnabled;
    object.transform.position = light.position;
    object.transform.rotationEulerDeg = light.rotationEulerDeg;
    object.components.push_back(SceneComponentData{kLightComponentType, WriteLightSettings(light.settings)});
    NormalizeSceneComponentIds(object);
    const SceneObjectId id = object.id;
    document.objects.push_back(std::move(object));
    return id;
}

} // namespace HIKARI::LIGHTING_AUTHORING
