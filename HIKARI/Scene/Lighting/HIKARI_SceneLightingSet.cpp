#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntimeConversion.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace {

std::string ReflectionProbeId(SceneObjectId objectId) { return "reflection_probe_" + std::to_string(objectId.value); }

std::string IrradianceVolumeId(SceneObjectId objectId) { return "irradiance_volume_" + std::to_string(objectId.value); }

std::string LightId(SceneObjectId objectId) { return "light_" + std::to_string(objectId.value); }

bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool EqualReflectionProvider(const RENDER3D::LIGHTING::ReflectionProbeProvider& lhs,
                             const RENDER3D::LIGHTING::ReflectionProbeProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && EqualVec3(lhs.position, rhs.position) &&
           lhs.radius == rhs.radius && lhs.intensity == rhs.intensity && lhs.influenceShape == rhs.influenceShape &&
           lhs.projectionShape == rhs.projectionShape && EqualVec3(lhs.influenceBoxCenter, rhs.influenceBoxCenter) &&
           EqualVec3(lhs.influenceBoxSize, rhs.influenceBoxSize) &&
           EqualVec3(lhs.projectionBoxCenter, rhs.projectionBoxCenter) &&
           EqualVec3(lhs.projectionBoxSize, rhs.projectionBoxSize) && lhs.blendDistance == rhs.blendDistance &&
           lhs.priority == rhs.priority && lhs.sourceCubemapAssetId == rhs.sourceCubemapAssetId &&
           lhs.bakeSet == rhs.bakeSet && lhs.scenario == rhs.scenario;
}

bool EqualIrradianceProvider(const RENDER3D::LIGHTING::IrradianceVolumeProvider& lhs,
                             const RENDER3D::LIGHTING::IrradianceVolumeProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && lhs.realtime == rhs.realtime &&
           EqualVec3(lhs.origin, rhs.origin) && EqualVec3(lhs.size, rhs.size) && EqualVec3(lhs.spacing, rhs.spacing) &&
           lhs.countX == rhs.countX && lhs.countY == rhs.countY && lhs.countZ == rhs.countZ &&
           lhs.intensity == rhs.intensity && lhs.blendDistance == rhs.blendDistance &&
           lhs.normalBias == rhs.normalBias && lhs.priority == rhs.priority && lhs.bakeSet == rhs.bakeSet &&
           lhs.scenario == rhs.scenario;
}

bool EqualLightProvider(const RENDER3D::LIGHTING::SceneLightProvider& lhs,
                        const RENDER3D::LIGHTING::SceneLightProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && lhs.type == rhs.type &&
           EqualVec3(lhs.position, rhs.position) && EqualVec3(lhs.direction, rhs.direction) &&
           EqualVec3(lhs.color, rhs.color) && lhs.intensity == rhs.intensity && lhs.range == rhs.range &&
           lhs.sourceRadius == rhs.sourceRadius && lhs.castsShadows == rhs.castsShadows &&
           lhs.innerConeAngleDegrees == rhs.innerConeAngleDegrees &&
           lhs.outerConeAngleDegrees == rhs.outerConeAngleDegrees && lhs.angularFalloff == rhs.angularFalloff &&
           lhs.priority == rhs.priority;
}

MATH::Vec3 WorldPosition(const MATH::Mat4& matrix) {
    const MATH::Vec4 position = matrix.TransformPoint({0.0f, 0.0f, 0.0f, 1.0f});
    return {position.x, position.y, position.z};
}

MATH::Vec3 WorldForward(const MATH::Mat4& matrix) {
    return MATH::Normalize({matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]});
}

} // namespace

RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(const SceneDocument& document) {
    RENDER3D::LIGHTING::SceneLightingSet set = BuildSceneLightingSet(document.objects);
    return set;
}

RENDER3D::LIGHTING::SceneLightingSet BuildRuntimeSceneLightingSet(const SceneDocument& document, const World& world) {
    RENDER3D::LIGHTING::SceneLightingSet set = BuildSceneLightingSet(document);
    set.lights.clear();
    std::unordered_set<uint64_t> disabledObjectIds{};
    for (const SceneObjectData& object : document.objects) {
        if (!object.enabled) {
            disabledObjectIds.insert(object.id.value);
        }
    }
    for (const std::unique_ptr<GameObject>& object : world.GetObjects()) {
        if (!object) {
            continue;
        }
        if (disabledObjectIds.contains(object->GetDocumentId().value)) {
            continue;
        }
        const LightComponent* component = object->GetComponent<LightComponent>();
        if (component == nullptr) {
            continue;
        }
        RENDER3D::LIGHTING::SceneLightProvider provider{};
        provider.id = LightId(object->GetDocumentId());
        provider.name = object->GetName();
        provider.enabled = component->IsEnabled();
        provider.type = component->GetLightType() == LightComponentType::Spot
                            ? RENDER3D::LIGHTING::SceneLightType::Spot
                            : RENDER3D::LIGHTING::SceneLightType::Point;
        const MATH::Mat4 worldMatrix = object->GetTransform().GetWorldMatrix();
        provider.position = WorldPosition(worldMatrix);
        provider.direction = WorldForward(worldMatrix);
        provider.color = component->GetColor();
        // 作者値は光束 [lm]、描画は光度 [cd]。
        provider.intensity = RENDER3D::LIGHTING::LuminousIntensityFromFlux(component->GetIntensity());
        provider.range = component->GetRange();
        provider.sourceRadius = component->GetSourceRadius();
        provider.castsShadows = component->CastsShadows();
        provider.innerConeAngleDegrees = component->GetInnerConeAngleDegrees();
        provider.outerConeAngleDegrees = component->GetOuterConeAngleDegrees();
        provider.angularFalloff = component->GetAngularFalloff();
        provider.priority = component->GetPriority();
        set.lights.push_back(std::move(provider));
    }
    std::stable_sort(set.lights.begin(), set.lights.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.id < rhs.id;
    });
    return set;
}

RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(std::span<const SceneObjectData> objects) {
    RENDER3D::LIGHTING::SceneLightingSet set{};

    for (const LightAuthoring& authored : CollectLights(objects)) {
        RENDER3D::LIGHTING::SceneLightProvider provider{};
        provider.id = LightId(authored.objectId);
        provider.name = authored.name;
        provider.enabled = authored.enabled;
        provider.type = authored.type;
        provider.position = authored.position;
        provider.direction = authored.direction;
        provider.color = authored.color;
        provider.intensity = RENDER3D::LIGHTING::LuminousIntensityFromFlux(authored.intensity);
        provider.range = authored.range;
        provider.sourceRadius = authored.sourceRadius;
        provider.castsShadows = authored.castsShadows;
        provider.innerConeAngleDegrees = authored.innerConeAngleDegrees;
        provider.outerConeAngleDegrees = authored.outerConeAngleDegrees;
        provider.angularFalloff = authored.angularFalloff;
        provider.priority = authored.priority;
        set.lights.push_back(std::move(provider));
    }

    for (const ReflectionProbeAuthoring& authored : CollectReflectionProbes(objects)) {
        RENDER3D::LIGHTING::ReflectionProbeProvider provider{};
        provider.id = ReflectionProbeId(authored.objectId);
        provider.name = authored.name;
        provider.enabled = authored.settings.enabled;
        provider.position = authored.settings.position;
        provider.radius = authored.settings.radius;
        provider.intensity = authored.settings.intensity;
        provider.influenceShape = REFLECTION::ToRuntimeInfluenceShape(authored.settings.influenceShape);
        provider.projectionShape = REFLECTION::ToRuntimeProjectionShape(authored.settings.projectionShape);
        provider.influenceBoxCenter = authored.settings.influenceBoxCenter;
        provider.influenceBoxSize = authored.settings.influenceBoxSize;
        provider.projectionBoxCenter = authored.settings.projectionBoxCenter;
        provider.projectionBoxSize = authored.settings.projectionBoxSize;
        provider.blendDistance = authored.settings.blendDistance;
        provider.priority = authored.settings.priority;
        provider.sourceCubemapAssetId = authored.settings.sourceCubemapAsset;
        provider.bakeSet = authored.bakeSet;
        provider.scenario = authored.scenario;
        set.reflectionProbes.push_back(std::move(provider));
    }

    for (const IrradianceVolumeAuthoring& authored : CollectIrradianceVolumes(objects)) {
        RENDER3D::LIGHTING::IrradianceVolumeProvider provider{};
        provider.id = IrradianceVolumeId(authored.objectId);
        provider.name = authored.name;
        provider.enabled = authored.settings.enabled;
        provider.realtime = authored.settings.mode == LightProbeVolumeMode::Realtime;
        provider.origin = authored.settings.origin;
        provider.size = authored.settings.size;
        provider.spacing = GetLightProbeVolumeSpacing(authored.settings);
        provider.countX = authored.settings.countX;
        provider.countY = authored.settings.countY;
        provider.countZ = authored.settings.countZ;
        provider.intensity = authored.settings.intensity;
        provider.blendDistance = authored.settings.blendDistance;
        provider.normalBias = authored.settings.normalBias;
        provider.priority = authored.priority;
        provider.bakeSet = authored.bakeSet;
        provider.scenario = authored.scenario;
        set.irradianceVolumes.push_back(std::move(provider));
    }

    return set;
}

bool AreSceneLightingSetsEquivalent(const RENDER3D::LIGHTING::SceneLightingSet& lhs,
                                    const RENDER3D::LIGHTING::SceneLightingSet& rhs) noexcept {
    return lhs.activeScenario == rhs.activeScenario && lhs.lights.size() == rhs.lights.size() &&
           lhs.reflectionProbes.size() == rhs.reflectionProbes.size() &&
           lhs.irradianceVolumes.size() == rhs.irradianceVolumes.size() &&
           std::equal(lhs.lights.begin(), lhs.lights.end(), rhs.lights.begin(), EqualLightProvider) &&
           std::equal(lhs.reflectionProbes.begin(), lhs.reflectionProbes.end(), rhs.reflectionProbes.begin(),
                      EqualReflectionProvider) &&
           std::equal(lhs.irradianceVolumes.begin(), lhs.irradianceVolumes.end(), rhs.irradianceVolumes.begin(),
                      EqualIrradianceProvider);
}

} // namespace HIKARI::LIGHTING_AUTHORING
