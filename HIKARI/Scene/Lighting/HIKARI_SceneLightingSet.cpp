#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntimeConversion.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace {

std::string ReflectionProbeId(SceneObjectId objectId) { return "reflection_probe_" + std::to_string(objectId.value); }

std::string IrradianceVolumeId(SceneObjectId objectId) { return "irradiance_volume_" + std::to_string(objectId.value); }

std::string LightId(SceneObjectId objectId) { return "light_" + std::to_string(objectId.value); }

std::string DecalId(SceneObjectId objectId) { return "decal_" + std::to_string(objectId.value); }

bool EqualReflectionProvider(const RENDER3D::LIGHTING::ReflectionProbeProvider& lhs,
                             const RENDER3D::LIGHTING::ReflectionProbeProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && lhs.position == rhs.position && lhs.radius == rhs.radius &&
           lhs.intensity == rhs.intensity && lhs.influenceShape == rhs.influenceShape &&
           lhs.projectionShape == rhs.projectionShape && lhs.influenceBoxCenter == rhs.influenceBoxCenter &&
           lhs.influenceBoxSize == rhs.influenceBoxSize && lhs.projectionBoxCenter == rhs.projectionBoxCenter &&
           lhs.projectionBoxSize == rhs.projectionBoxSize && lhs.blendDistance == rhs.blendDistance &&
           lhs.priority == rhs.priority && lhs.sourceCubemapAssetId == rhs.sourceCubemapAssetId &&
           lhs.bakeSet == rhs.bakeSet && lhs.scenario == rhs.scenario;
}

bool EqualIrradianceProvider(const RENDER3D::LIGHTING::IrradianceVolumeProvider& lhs,
                             const RENDER3D::LIGHTING::IrradianceVolumeProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && lhs.realtime == rhs.realtime && lhs.origin == rhs.origin &&
           lhs.size == rhs.size && lhs.spacing == rhs.spacing && lhs.countX == rhs.countX && lhs.countY == rhs.countY &&
           lhs.countZ == rhs.countZ && lhs.intensity == rhs.intensity && lhs.blendDistance == rhs.blendDistance &&
           lhs.normalBias == rhs.normalBias && lhs.priority == rhs.priority && lhs.bakeSet == rhs.bakeSet &&
           lhs.scenario == rhs.scenario;
}

bool EqualLightProvider(const RENDER3D::LIGHTING::SceneLightProvider& lhs,
                        const RENDER3D::LIGHTING::SceneLightProvider& rhs) {
    return lhs.id == rhs.id && lhs.enabled == rhs.enabled && lhs.type == rhs.type && lhs.position == rhs.position &&
           lhs.direction == rhs.direction && lhs.tangent == rhs.tangent && lhs.length == rhs.length &&
           lhs.height == rhs.height && lhs.color == rhs.color && lhs.intensity == rhs.intensity &&
           lhs.range == rhs.range && lhs.sourceRadius == rhs.sourceRadius && lhs.castsShadows == rhs.castsShadows &&
           lhs.innerConeAngleDegrees == rhs.innerConeAngleDegrees &&
           lhs.outerConeAngleDegrees == rhs.outerConeAngleDegrees && lhs.angularFalloff == rhs.angularFalloff &&
           lhs.priority == rhs.priority && lhs.iesProfile == rhs.iesProfile && lhs.cookie == rhs.cookie;
}

MATH::Vec3 WorldPosition(const MATH::Mat4& matrix) {
    const MATH::Vec4 position = matrix.TransformPoint({0.0f, 0.0f, 0.0f, 1.0f});
    return {position.x, position.y, position.z};
}

MATH::Vec3 WorldForward(const MATH::Mat4& matrix) {
    return MATH::Normalize({matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]});
}

MATH::Vec3 WorldRight(const MATH::Mat4& matrix) {
    return MATH::Normalize({matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]});
}

// コンポーネントと文書のどちらの作者値からも同じ形の光源を作る。作者値は光束 [lm] か相対の倍率、描画は光度 [cd]。
RENDER3D::LIGHTING::SceneLightProvider BuildLightProvider(std::string id, std::string name,
                                                          const LightSettings& settings, const MATH::Vec3& position,
                                                          const MATH::Vec3& forward, const MATH::Vec3& right) {
    RENDER3D::LIGHTING::SceneLightProvider provider{};
    provider.id = std::move(id);
    provider.name = std::move(name);
    provider.enabled = settings.enabled;
    provider.type = settings.type;
    provider.position = position;
    provider.direction = forward;
    provider.tangent = right;
    provider.color = settings.color;
    provider.intensity = ResolveLuminousIntensity(settings);
    provider.range = settings.range;
    provider.sourceRadius = settings.sourceRadius;
    provider.length = settings.length;
    provider.height = settings.height;
    provider.castsShadows = settings.castsShadows;
    provider.innerConeAngleDegrees = settings.innerConeAngleDegrees;
    provider.outerConeAngleDegrees = settings.outerConeAngleDegrees;
    provider.angularFalloff = settings.angularFalloff;
    provider.priority = settings.priority;
    provider.iesProfile = settings.iesProfile;
    provider.cookie = settings.type == RENDER3D::LIGHTING::SceneLightType::Spot ? settings.cookie : std::string{};
    return provider;
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
        const LightComponent* light = object->GetComponent<LightComponent>();
        const DecalComponent* decal = object->GetComponent<DecalComponent>();
        const bool hasDecal = decal != nullptr && decal->GetSettings().enabled;
        if (light == nullptr && !hasDecal) {
            continue;
        }
        const MATH::Mat4 worldMatrix = object->GetTransform().GetWorldMatrix();
        if (light != nullptr) {
            set.lights.push_back(BuildLightProvider(LightId(object->GetDocumentId()), object->GetName(),
                                                    light->GetSettings(), WorldPosition(worldMatrix),
                                                    WorldForward(worldMatrix), WorldRight(worldMatrix)));
        }
        if (hasDecal) {
            set.decals.push_back({DecalId(object->GetDocumentId()), worldMatrix, decal->GetSettings()});
        }
    }
    std::stable_sort(set.lights.begin(), set.lights.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.id < rhs.id;
    });
    std::stable_sort(set.decals.begin(), set.decals.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.settings.sortOrder != rhs.settings.sortOrder) {
            return lhs.settings.sortOrder < rhs.settings.sortOrder;
        }
        return lhs.id < rhs.id;
    });
    return set;
}

RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(std::span<const SceneObjectData> objects) {
    RENDER3D::LIGHTING::SceneLightingSet set{};

    for (const LightAuthoring& authored : CollectLights(objects)) {
        set.lights.push_back(BuildLightProvider(LightId(authored.objectId), authored.name, authored.settings,
                                                authored.position, authored.direction, authored.tangent));
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
