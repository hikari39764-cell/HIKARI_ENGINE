#include "Scene/Lighting/HIKARI_SceneLightingSet.h"

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

    } // namespace

    RENDER3D::LIGHTING::SceneLightingSet BuildSceneLightingSet(
        const SceneDocument& document) {

        RENDER3D::LIGHTING::SceneLightingSet set{};

        for (const ReflectionProbeAuthoring& authored :
             CollectReflectionProbes(document)) {
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
             CollectIrradianceVolumes(document)) {
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

} // namespace HIKARI::LIGHTING_AUTHORING
