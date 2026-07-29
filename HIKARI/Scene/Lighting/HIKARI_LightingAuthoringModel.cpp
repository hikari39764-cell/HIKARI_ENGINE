#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/Lighting/Internal/HIKARI_LightAuthoringInternal.h"

namespace HIKARI::LIGHTING_AUTHORING {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {

        SceneObjectId AllocateSceneObjectId(const SceneDocument& document) {
            uint64_t maximumId = 0;
            for (const SceneObjectData& object : document.objects) {
                maximumId = (std::max)(maximumId, object.id.value);
            }
            return SceneObjectId{ maximumId + 1u };
        }

        std::string NonEmptyOrDefault(
            const nlohmann::json& properties,
            const char* name) {

            const std::string value = properties.value(
                name,
                std::string("Default"));
            return value.empty() ? "Default" : value;
        }

        ReflectionProbeInfluenceShape ReadInfluenceShape(
            const nlohmann::json& properties) {

            return properties.value(
                "influenceShape",
                std::string("Sphere")) == "Box"
                ? ReflectionProbeInfluenceShape::Box
                : ReflectionProbeInfluenceShape::Sphere;
        }

        ReflectionProbeProjectionShape ReadProjectionShape(
            const nlohmann::json& properties) {

            return properties.value(
                "projectionShape",
                std::string("Infinite")) == "Box"
                ? ReflectionProbeProjectionShape::Box
                : ReflectionProbeProjectionShape::Infinite;
        }

        ReflectionProbeAuthoring ReadReflectionProbe(
            const SceneObjectData& object,
            const SceneComponentData& component) {

            const nlohmann::json& properties = component.properties;
            ReflectionProbeAuthoring probe{};
            probe.objectId = object.id;
            probe.name = object.name;
            probe.position = object.transform.position;
            probe.settings.enabled =
                object.enabled && properties.value("enabled", true);
            probe.settings.sourceCubemapAsset = properties.value(
                "sourceCubemapAsset",
                std::string{});
            probe.settings.position = probe.position;
            probe.settings.radius = (std::max)(
                properties.value("radius", 8.0f),
                0.05f);
            probe.settings.intensity = std::clamp(
                properties.value("intensity", 1.0f),
                0.0f,
                8.0f);
            probe.settings.influenceShape =
                ReadInfluenceShape(properties);
            const MATH::Vec3 influenceOffset = JSONREAD::Vec3Or(
                properties.value(
                    "influenceBoxOffset",
                    nlohmann::json::array()),
                {});
            probe.settings.influenceBoxCenter =
                probe.position + influenceOffset;
            probe.settings.influenceBoxSize = JSONREAD::Vec3Or(
                properties.value(
                    "influenceBoxSize",
                    nlohmann::json::array()),
                { 8.0f, 4.0f, 8.0f });
            probe.settings.projectionShape =
                ReadProjectionShape(properties);
            const MATH::Vec3 projectionOffset = JSONREAD::Vec3Or(
                properties.value(
                    "projectionBoxOffset",
                    nlohmann::json::array()),
                {});
            probe.settings.projectionBoxCenter =
                probe.position + projectionOffset;
            probe.settings.projectionBoxSize = JSONREAD::Vec3Or(
                properties.value(
                    "projectionBoxSize",
                    nlohmann::json::array()),
                { 8.0f, 4.0f, 8.0f });
            probe.settings.blendDistance = (std::max)(
                properties.value("blendDistance", 1.0f),
                0.0f);
            probe.settings.priority = properties.value("priority", 0);
            probe.captureResolution = properties.value(
                "captureResolution",
                128u);
            probe.bakeSet = NonEmptyOrDefault(properties, "bakeSet");
            probe.scenario = NonEmptyOrDefault(properties, "scenario");
            return probe;
        }

        IrradianceVolumeAuthoring ReadIrradianceVolume(
            const SceneObjectData& object,
            const SceneComponentData& component) {

            const nlohmann::json& properties = component.properties;
            IrradianceVolumeAuthoring volume{};
            volume.objectId = object.id;
            volume.name = object.name;
            volume.center = object.transform.position;
            volume.settings.enabled =
                object.enabled && properties.value("enabled", true);
            volume.settings.size = JSONREAD::Vec3Or(
                properties.value("size", nlohmann::json::array()),
                { 10.0f, 4.0f, 10.0f });
            volume.settings.origin = {
                volume.center.x - volume.settings.size.x * 0.5f,
                volume.center.y - volume.settings.size.y * 0.5f,
                volume.center.z - volume.settings.size.z * 0.5f
            };
            volume.settings.countX = properties.value("countX", 4u);
            volume.settings.countY = properties.value("countY", 2u);
            volume.settings.countZ = properties.value("countZ", 4u);
            volume.settings.intensity = properties.value(
                "intensity",
                1.0f);
            volume.settings.blendDistance = properties.value(
                "blendDistance",
                1.0f);
            volume.settings.normalBias = properties.value(
                "normalBias",
                0.2f);
            volume.settings.captureResolution = properties.value(
                "captureResolution",
                32u);
            volume.settings.shOrder = properties.value("shOrder", 3u);
            ClampLightProbeVolumeSettings(volume.settings);
            volume.priority = properties.value("priority", 0);
            volume.bakeSet = NonEmptyOrDefault(properties, "bakeSet");
            volume.scenario = NonEmptyOrDefault(properties, "scenario");
            return volume;
        }

        bool HasLightingComponent(
            const SceneDocument& document,
            const char* componentType) {

            return std::any_of(
                document.objects.begin(),
                document.objects.end(),
                [componentType](const SceneObjectData& object) {
                    return FindLightingComponent(
                        object,
                        componentType) != nullptr;
                });
        }

    } // namespace

    const SceneComponentData* FindLightingComponent(
        const SceneObjectData& object,
        const char* componentType) {

        const auto found = std::find_if(
            object.components.begin(),
            object.components.end(),
            [componentType](const SceneComponentData& component) {
                return component.type == componentType;
            });
        return found == object.components.end() ? nullptr : &*found;
    }

    SceneComponentData* FindLightingComponent(
        SceneObjectData& object,
        const char* componentType) {

        const auto found = std::find_if(
            object.components.begin(),
            object.components.end(),
            [componentType](const SceneComponentData& component) {
                return component.type == componentType;
            });
        return found == object.components.end() ? nullptr : &*found;
    }

    std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(
        const SceneDocument& document) {

        return CollectReflectionProbes(document.objects);
    }

    std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(
        std::span<const SceneObjectData> objects) {

        std::vector<ReflectionProbeAuthoring> probes{};
        for (const SceneObjectData& object : objects) {
            const SceneComponentData* component = FindLightingComponent(
                object,
                kReflectionProbeComponentType);
            if (component != nullptr && component->properties.is_object()) {
                probes.push_back(ReadReflectionProbe(object, *component));
            }
        }
        std::stable_sort(
            probes.begin(),
            probes.end(),
            [](const ReflectionProbeAuthoring& lhs,
               const ReflectionProbeAuthoring& rhs) {
                if (lhs.settings.priority != rhs.settings.priority) {
                    return lhs.settings.priority > rhs.settings.priority;
                }
                return lhs.objectId.value < rhs.objectId.value;
            });
        return probes;
    }

    std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(
        const SceneDocument& document) {

        return CollectIrradianceVolumes(document.objects);
    }

    std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(
        std::span<const SceneObjectData> objects) {

        std::vector<IrradianceVolumeAuthoring> volumes{};
        for (const SceneObjectData& object : objects) {
            const SceneComponentData* component = FindLightingComponent(
                object,
                kIrradianceVolumeComponentType);
            if (component != nullptr && component->properties.is_object()) {
                volumes.push_back(ReadIrradianceVolume(object, *component));
            }
        }
        std::stable_sort(
            volumes.begin(),
            volumes.end(),
            [](const IrradianceVolumeAuthoring& lhs,
               const IrradianceVolumeAuthoring& rhs) {
                if (lhs.priority != rhs.priority) {
                    return lhs.priority > rhs.priority;
                }
                return lhs.objectId.value < rhs.objectId.value;
            });
        return volumes;
    }

    SceneObjectId AddReflectionProbe(
        SceneDocument& document,
        const ReflectionProbeAuthoring& probe) {

        SceneObjectData object{};
        object.id = probe.objectId.value != 0u
            ? probe.objectId
            : AllocateSceneObjectId(document);
        object.name = probe.name.empty()
            ? "Reflection Probe"
            : probe.name;
        object.transform.position = probe.position;
        object.components.push_back(SceneComponentData{
            kReflectionProbeComponentType,
            {
                { "enabled", probe.settings.enabled },
                {
                    "sourceCubemapAsset",
                    probe.settings.sourceCubemapAsset
                },
                { "radius", probe.settings.radius },
                { "intensity", probe.settings.intensity },
                {
                    "influenceShape",
                    probe.settings.influenceShape ==
                        ReflectionProbeInfluenceShape::Box
                        ? "Box"
                        : "Sphere"
                },
                {
                    "influenceBoxOffset",
                    JsonMath::ToJsonArray(
                        probe.settings.influenceBoxCenter -
                        probe.position)
                },
                {
                    "influenceBoxSize",
                    JsonMath::ToJsonArray(
                        probe.settings.influenceBoxSize)
                },
                {
                    "projectionShape",
                    probe.settings.projectionShape ==
                        ReflectionProbeProjectionShape::Box
                        ? "Box"
                        : "Infinite"
                },
                {
                    "projectionBoxOffset",
                    JsonMath::ToJsonArray(
                        probe.settings.projectionBoxCenter -
                        probe.position)
                },
                {
                    "projectionBoxSize",
                    JsonMath::ToJsonArray(
                        probe.settings.projectionBoxSize)
                },
                { "blendDistance", probe.settings.blendDistance },
                { "priority", probe.settings.priority },
                { "captureResolution", probe.captureResolution },
                { "bakeSet", probe.bakeSet },
                { "scenario", probe.scenario }
            }
        });
        const SceneObjectId id = object.id;
        document.objects.push_back(std::move(object));
        return id;
    }

    SceneObjectId AddIrradianceVolume(
        SceneDocument& document,
        const IrradianceVolumeAuthoring& volume) {

        LightProbeVolumeSettings settings = volume.settings;
        ClampLightProbeVolumeSettings(settings);
        SceneObjectData object{};
        object.id = volume.objectId.value != 0u
            ? volume.objectId
            : AllocateSceneObjectId(document);
        object.name = volume.name.empty()
            ? "Irradiance Volume"
            : volume.name;
        object.transform.position = volume.center;
        object.components.push_back(SceneComponentData{
            kIrradianceVolumeComponentType,
            {
                { "enabled", settings.enabled },
                { "size", JsonMath::ToJsonArray(settings.size) },
                { "countX", settings.countX },
                { "countY", settings.countY },
                { "countZ", settings.countZ },
                { "intensity", settings.intensity },
                { "blendDistance", settings.blendDistance },
                { "normalBias", settings.normalBias },
                { "captureResolution", settings.captureResolution },
                { "shOrder", settings.shOrder },
                { "priority", volume.priority },
                { "bakeSet", volume.bakeSet },
                { "scenario", volume.scenario }
            }
        });
        const SceneObjectId id = object.id;
        document.objects.push_back(std::move(object));
        return id;
    }

    void MigrateLegacyLightingAuthoring(
        SceneDocument& document,
        uint32_t sourceVersion) {

        if (sourceVersion < 5u &&
            !HasLightingComponent(
                document,
                kReflectionProbeComponentType) &&
            document.environment.reflectionProbe.enabled) {

            const ReflectionProbeSettings& legacy =
                document.environment.reflectionProbe;
            ReflectionProbeAuthoring probe{};
            probe.name = "Reflection Probe";
            probe.position = legacy.position;
            probe.settings = legacy;
            (void)AddReflectionProbe(document, probe);
        }

        if (sourceVersion < 5u &&
            !HasLightingComponent(
                document,
                kIrradianceVolumeComponentType) &&
            document.lightingBake.lightProbeVolume.enabled) {

            LightProbeVolumeSettings legacy =
                document.lightingBake.lightProbeVolume;
            ClampLightProbeVolumeSettings(legacy);
            IrradianceVolumeAuthoring volume{};
            volume.name = "Irradiance Volume";
            volume.center = {
                legacy.origin.x + legacy.size.x * 0.5f,
                legacy.origin.y + legacy.size.y * 0.5f,
                legacy.origin.z + legacy.size.z * 0.5f
            };
            volume.settings = legacy;
            (void)AddIrradianceVolume(document, volume);
        }

        INTERNAL::MigrateLegacyLights(
            document,
            sourceVersion);
    }

    void SynchronizeLegacyLightingViews(SceneDocument& document) {
        const std::vector<ReflectionProbeAuthoring> probes =
            CollectReflectionProbes(document);
        const auto reflection = std::find_if(
            probes.begin(),
            probes.end(),
            [](const ReflectionProbeAuthoring& probe) {
                return probe.settings.enabled;
            });
        if (reflection == probes.end()) {
            document.environment.reflectionProbe.enabled = false;
        } else {
            document.environment.reflectionProbe =
                reflection->settings;
        }

        const std::vector<IrradianceVolumeAuthoring> volumes =
            CollectIrradianceVolumes(document);
        const auto irradiance = std::find_if(
            volumes.begin(),
            volumes.end(),
            [](const IrradianceVolumeAuthoring& volume) {
                return volume.settings.enabled;
            });
        if (irradiance == volumes.end()) {
            document.lightingBake.lightProbeVolume.enabled = false;
        } else {
            document.lightingBake.lightProbeVolume =
                irradiance->settings;
        }
    }

} // namespace HIKARI::LIGHTING_AUTHORING
