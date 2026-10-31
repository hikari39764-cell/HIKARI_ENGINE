#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::LIGHTING_AUTHORING {

namespace {

// 部件と同じ項目表で読む（緩い読み）。位置は物体の位置からの相対で保存されているので、絶対座標へ直して渡す。
ReflectionProbeAuthoring ReadReflectionProbe(const SceneObjectData& object, const SceneComponentData& component) {
    ReflectionProbeComponentSettings authored{};
    JSONFIELDS::ReadLenient(component.properties, authored, kReflectionProbeFields);
    authored = NormalizeReflectionProbeSettings(authored);

    ReflectionProbeAuthoring probe{};
    probe.objectId = object.id;
    probe.name = object.name;
    probe.position = object.transform.position;
    probe.settings.enabled = object.enabled && authored.enabled;
    probe.settings.sourceCubemapAsset = authored.sourceCubemapAsset;
    probe.settings.position = probe.position;
    probe.settings.radius = authored.radius;
    probe.settings.intensity = authored.intensity;
    probe.settings.influenceShape = authored.influenceShape;
    probe.settings.influenceBoxCenter = probe.position + authored.influenceBoxOffset;
    probe.settings.influenceBoxSize = authored.influenceBoxSize;
    probe.settings.projectionShape = authored.projectionShape;
    probe.settings.projectionBoxCenter = probe.position + authored.projectionBoxOffset;
    probe.settings.projectionBoxSize = authored.projectionBoxSize;
    probe.settings.blendDistance = authored.blendDistance;
    probe.settings.priority = authored.priority;
    probe.captureResolution = authored.captureResolution;
    probe.bakeSet = authored.bakeSet;
    probe.scenario = authored.scenario;
    return probe;
}

IrradianceVolumeAuthoring ReadIrradianceVolume(const SceneObjectData& object, const SceneComponentData& component) {
    IrradianceVolumeComponentSettings authored{};
    JSONFIELDS::ReadLenient(component.properties, authored, kIrradianceVolumeFields);
    authored = NormalizeIrradianceVolumeSettings(authored);

    IrradianceVolumeAuthoring volume{};
    volume.objectId = object.id;
    volume.name = object.name;
    volume.center = object.transform.position;
    volume.settings = authored;
    volume.settings.enabled = object.enabled && authored.enabled;
    volume.settings.origin = {volume.center.x - authored.size.x * 0.5f, volume.center.y - authored.size.y * 0.5f,
                              volume.center.z - authored.size.z * 0.5f};
    volume.priority = authored.priority;
    volume.bakeSet = authored.bakeSet;
    volume.scenario = authored.scenario;
    return volume;
}

bool HasLightingComponent(const SceneDocument& document, const char* componentType) {
    return std::any_of(
        document.objects.begin(), document.objects.end(),
        [componentType](const SceneObjectData& object) { return object.FindComponent(componentType) != nullptr; });
}

} // namespace

bool IsLightingComponentType(std::string_view componentType) noexcept {
    return componentType == kLightComponentType || componentType == kReflectionProbeComponentType ||
           componentType == kIrradianceVolumeComponentType;
}

std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(const SceneDocument& document) {
    return CollectReflectionProbes(document.objects);
}

std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(std::span<const SceneObjectData> objects) {
    std::vector<ReflectionProbeAuthoring> probes{};
    for (const SceneObjectData& object : objects) {
        const SceneComponentData* component = object.FindComponent(kReflectionProbeComponentType);
        if (component != nullptr && component->properties.is_object()) {
            probes.push_back(ReadReflectionProbe(object, *component));
        }
    }
    std::stable_sort(probes.begin(), probes.end(),
                     [](const ReflectionProbeAuthoring& lhs, const ReflectionProbeAuthoring& rhs) {
                         if (lhs.settings.priority != rhs.settings.priority) {
                             return lhs.settings.priority > rhs.settings.priority;
                         }
                         return lhs.objectId.value < rhs.objectId.value;
                     });
    return probes;
}

std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(const SceneDocument& document) {
    return CollectIrradianceVolumes(document.objects);
}

std::vector<IrradianceVolumeAuthoring> CollectBakedIrradianceVolumes(const SceneDocument& document) {
    std::vector<IrradianceVolumeAuthoring> volumes = CollectIrradianceVolumes(document);
    std::erase_if(volumes, [](const IrradianceVolumeAuthoring& volume) {
        return volume.settings.mode != LightProbeVolumeMode::Baked;
    });
    return volumes;
}

std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(std::span<const SceneObjectData> objects) {
    std::vector<IrradianceVolumeAuthoring> volumes{};
    for (const SceneObjectData& object : objects) {
        const SceneComponentData* component = object.FindComponent(kIrradianceVolumeComponentType);
        if (component != nullptr && component->properties.is_object()) {
            volumes.push_back(ReadIrradianceVolume(object, *component));
        }
    }
    std::stable_sort(volumes.begin(), volumes.end(),
                     [](const IrradianceVolumeAuthoring& lhs, const IrradianceVolumeAuthoring& rhs) {
                         if (lhs.priority != rhs.priority) {
                             return lhs.priority > rhs.priority;
                         }
                         return lhs.objectId.value < rhs.objectId.value;
                     });
    return volumes;
}

SceneObjectId AddReflectionProbe(SceneDocument& document, const ReflectionProbeAuthoring& probe) {
    SceneObjectData object{};
    object.id = probe.objectId.value != 0u ? probe.objectId : AllocateSceneObjectId(document);
    object.name = probe.name.empty() ? "Reflection Probe" : probe.name;
    object.transform.position = probe.position;
    // 投影の箱は probe.settings が明示するので、影響範囲には連動させない。
    ReflectionProbeComponentSettings authored{};
    authored.projectionFollowsInfluence = false;
    authored.enabled = probe.settings.enabled;
    authored.sourceCubemapAsset = probe.settings.sourceCubemapAsset;
    authored.radius = probe.settings.radius;
    authored.intensity = probe.settings.intensity;
    authored.influenceShape = probe.settings.influenceShape;
    authored.influenceBoxOffset = probe.settings.influenceBoxCenter - probe.position;
    authored.influenceBoxSize = probe.settings.influenceBoxSize;
    authored.projectionShape = probe.settings.projectionShape;
    authored.projectionBoxOffset = probe.settings.projectionBoxCenter - probe.position;
    authored.projectionBoxSize = probe.settings.projectionBoxSize;
    authored.blendDistance = probe.settings.blendDistance;
    authored.priority = probe.settings.priority;
    authored.captureResolution = probe.captureResolution;
    authored.bakeSet = probe.bakeSet;
    authored.scenario = probe.scenario;
    nlohmann::json properties = nlohmann::json::object();
    JSONFIELDS::Write(properties, NormalizeReflectionProbeSettings(authored), kReflectionProbeFields);
    object.components.push_back(SceneComponentData{kReflectionProbeComponentType, std::move(properties)});
    NormalizeSceneComponentIds(object);
    const SceneObjectId id = object.id;
    document.objects.push_back(std::move(object));
    return id;
}

SceneObjectId AddIrradianceVolume(SceneDocument& document, const IrradianceVolumeAuthoring& volume) {
    IrradianceVolumeComponentSettings authored{};
    static_cast<LightProbeVolumeSettings&>(authored) = volume.settings;
    authored.priority = volume.priority;
    authored.bakeSet = volume.bakeSet;
    authored.scenario = volume.scenario;
    SceneObjectData object{};
    object.id = volume.objectId.value != 0u ? volume.objectId : AllocateSceneObjectId(document);
    object.name = volume.name.empty() ? "Irradiance Volume" : volume.name;
    object.transform.position = volume.center;
    nlohmann::json properties = nlohmann::json::object();
    JSONFIELDS::Write(properties, NormalizeIrradianceVolumeSettings(authored), kIrradianceVolumeFields);
    object.components.push_back(SceneComponentData{kIrradianceVolumeComponentType, std::move(properties)});
    NormalizeSceneComponentIds(object);
    const SceneObjectId id = object.id;
    document.objects.push_back(std::move(object));
    return id;
}

} // namespace HIKARI::LIGHTING_AUTHORING
