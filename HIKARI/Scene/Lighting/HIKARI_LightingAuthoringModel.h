#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::LIGHTING_AUTHORING {

    inline constexpr const char* kReflectionProbeComponentType =
        "ReflectionProbeComponent";
    inline constexpr const char* kIrradianceVolumeComponentType =
        "IrradianceVolumeComponent";

    struct ReflectionProbeAuthoring {
        SceneObjectId objectId{};
        std::string name{};
        MATH::Vec3 position{};
        ReflectionProbeSettings settings{};
        uint32_t captureResolution = 128;
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    struct IrradianceVolumeAuthoring {
        SceneObjectId objectId{};
        std::string name{};
        MATH::Vec3 center{};
        LightProbeVolumeSettings settings{};
        int priority = 0;
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    std::vector<ReflectionProbeAuthoring> CollectReflectionProbes(
        const SceneDocument& document);
    std::vector<IrradianceVolumeAuthoring> CollectIrradianceVolumes(
        const SceneDocument& document);

    SceneObjectId AddReflectionProbe(
        SceneDocument& document,
        const ReflectionProbeAuthoring& probe);
    SceneObjectId AddIrradianceVolume(
        SceneDocument& document,
        const IrradianceVolumeAuthoring& volume);

    const SceneComponentData* FindLightingComponent(
        const SceneObjectData& object,
        const char* componentType);
    SceneComponentData* FindLightingComponent(
        SceneObjectData& object,
        const char* componentType);

    void MigrateLegacyLightingAuthoring(
        SceneDocument& document,
        uint32_t sourceVersion);
    void SynchronizeLegacyLightingViews(SceneDocument& document);

} // namespace HIKARI::LIGHTING_AUTHORING
