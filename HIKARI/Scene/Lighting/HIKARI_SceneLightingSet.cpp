#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntimeConversion.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

namespace HIKARI::LIGHTING_AUTHORING {

    namespace {

        std::string ReflectionProbeId(SceneObjectId objectId) {
            return "reflection_probe_" + std::to_string(objectId.value);
        }

        std::string IrradianceVolumeId(SceneObjectId objectId) {
            return "irradiance_volume_" + std::to_string(objectId.value);
        }

        bool EqualVec3(
            const MATH::Vec3& lhs,
            const MATH::Vec3& rhs) noexcept {

            return lhs.x == rhs.x &&
                lhs.y == rhs.y &&
                lhs.z == rhs.z;
        }

        bool EqualReflectionProvider(
            const RENDER3D::LIGHTING::ReflectionProbeProvider& lhs,
            const RENDER3D::LIGHTING::ReflectionProbeProvider& rhs) {

            return lhs.id == rhs.id &&
                lhs.enabled == rhs.enabled &&
                EqualVec3(lhs.position, rhs.position) &&
                lhs.radius == rhs.radius &&
                lhs.intensity == rhs.intensity &&
                lhs.influenceShape == rhs.influenceShape &&
                lhs.projectionShape == rhs.projectionShape &&
                EqualVec3(
                    lhs.influenceBoxCenter,
                    rhs.influenceBoxCenter) &&
                EqualVec3(
                    lhs.influenceBoxSize,
                    rhs.influenceBoxSize) &&
                EqualVec3(
                    lhs.projectionBoxCenter,
                    rhs.projectionBoxCenter) &&
                EqualVec3(
                    lhs.projectionBoxSize,
                    rhs.projectionBoxSize) &&
                lhs.blendDistance == rhs.blendDistance &&
                lhs.priority == rhs.priority &&
                lhs.sourceCubemapAssetId ==
                    rhs.sourceCubemapAssetId &&
                lhs.bakeSet == rhs.bakeSet &&
                lhs.scenario == rhs.scenario;
        }

        bool EqualIrradianceProvider(
            const RENDER3D::LIGHTING::IrradianceVolumeProvider& lhs,
            const RENDER3D::LIGHTING::IrradianceVolumeProvider& rhs) {

            return lhs.id == rhs.id &&
                lhs.enabled == rhs.enabled &&
                EqualVec3(lhs.origin, rhs.origin) &&
                EqualVec3(lhs.size, rhs.size) &&
                lhs.countX == rhs.countX &&
                lhs.countY == rhs.countY &&
                lhs.countZ == rhs.countZ &&
                lhs.intensity == rhs.intensity &&
                lhs.blendDistance == rhs.blendDistance &&
                lhs.priority == rhs.priority &&
                lhs.bakeSet == rhs.bakeSet &&
                lhs.scenario == rhs.scenario;
        }

    } // namespace

    RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(
        const SceneDocument& document) {

        RENDER3D::LIGHTING::SceneLightingSet set =
            BuildSceneLightingSet(document.objects);
        return set;
    }

    RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(
        std::span<const SceneObjectData> objects) {

        RENDER3D::LIGHTING::SceneLightingSet set{};

        for (const ReflectionProbeAuthoring& authored :
             CollectReflectionProbes(objects)) {
            RENDER3D::LIGHTING::ReflectionProbeProvider provider{};
            provider.id = ReflectionProbeId(authored.objectId);
            provider.name = authored.name;
            provider.enabled = authored.settings.enabled;
            provider.position = authored.settings.position;
            provider.radius = authored.settings.radius;
            provider.intensity = authored.settings.intensity;
            provider.influenceShape = REFLECTION::ToRuntimeInfluenceShape(
                authored.settings.influenceShape);
            provider.projectionShape = REFLECTION::ToRuntimeProjectionShape(
                authored.settings.projectionShape);
            provider.influenceBoxCenter =
                authored.settings.influenceBoxCenter;
            provider.influenceBoxSize =
                authored.settings.influenceBoxSize;
            provider.projectionBoxCenter =
                authored.settings.projectionBoxCenter;
            provider.projectionBoxSize =
                authored.settings.projectionBoxSize;
            provider.blendDistance = authored.settings.blendDistance;
            provider.priority = authored.settings.priority;
            provider.sourceCubemapAssetId =
                authored.settings.sourceCubemapAsset;
            provider.bakeSet = authored.bakeSet;
            provider.scenario = authored.scenario;
            set.reflectionProbes.push_back(std::move(provider));
        }

        for (const IrradianceVolumeAuthoring& authored :
             CollectIrradianceVolumes(objects)) {
            RENDER3D::LIGHTING::IrradianceVolumeProvider provider{};
            provider.id = IrradianceVolumeId(authored.objectId);
            provider.name = authored.name;
            provider.enabled = authored.settings.enabled;
            provider.origin = authored.settings.origin;
            provider.size = authored.settings.size;
            provider.countX = authored.settings.countX;
            provider.countY = authored.settings.countY;
            provider.countZ = authored.settings.countZ;
            provider.intensity = authored.settings.intensity;
            provider.blendDistance =
                authored.settings.blendDistance;
            provider.priority = authored.priority;
            provider.bakeSet = authored.bakeSet;
            provider.scenario = authored.scenario;
            set.irradianceVolumes.push_back(std::move(provider));
        }

        return set;
    }

    bool AreSceneLightingSetsEquivalent(
        const RENDER3D::LIGHTING::SceneLightingSet& lhs,
        const RENDER3D::LIGHTING::SceneLightingSet& rhs) noexcept {

        return lhs.activeScenario == rhs.activeScenario &&
            lhs.reflectionProbes.size() ==
                rhs.reflectionProbes.size() &&
            lhs.irradianceVolumes.size() ==
                rhs.irradianceVolumes.size() &&
            std::equal(
                lhs.reflectionProbes.begin(),
                lhs.reflectionProbes.end(),
                rhs.reflectionProbes.begin(),
                EqualReflectionProvider) &&
            std::equal(
                lhs.irradianceVolumes.begin(),
                lhs.irradianceVolumes.end(),
                rhs.irradianceVolumes.begin(),
                EqualIrradianceProvider);
    }

} // namespace HIKARI::LIGHTING_AUTHORING
