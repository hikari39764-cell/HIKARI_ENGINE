#pragma once

#include <cstdint>
#include <string>

namespace HIKARI::RENDER3D::LIGHTING {

    enum class LightingRuntimeSource {
        None,
        AuthoringFallback,
        BakeManifestDiscovered,
        BakedRuntime,
    };

    struct SceneLightingRuntimeData {
        LightingRuntimeSource source = LightingRuntimeSource::None;
        bool skyLoaded = false;
        bool globalIblLoaded = false;
        bool reflectionProbeLoaded = false;
        bool lightProbeVolumeLoaded = false;
        bool lightmapLoaded = false;
        bool bakeManifestLoaded = false;
        bool bakeManifestStale = false;
        std::string bakeManifestStaleReason{};
        std::string activeScenario{ "Default" };
        std::string activeSkyAssetId{};
        std::string activeReflectionProbeSourceAssetId{};
        std::string activeLightProbeVolumePath{};
        std::string bakeManifestPath{};
        uint32_t bakedReflectionProbeCount = 0;
        uint32_t bakedLightProbeCount = 0;
        uint32_t bakedLightmapCount = 0;
        uint32_t loadedReflectionProbeCount = 0;
        uint32_t loadedIrradianceVolumeCount = 0;
        uint32_t loadedLightmapCount = 0;
        uint32_t staleReflectionProbeCount = 0;
        uint32_t staleIrradianceVolumeCount = 0;
        uint32_t staleLightmapCount = 0;
    };

    const char* ToString(LightingRuntimeSource source);

    const SceneLightingRuntimeData& GetLastLightingRuntimeData();
    void SetLastLightingRuntimeData(const SceneLightingRuntimeData& data);

} // namespace HIKARI::RENDER3D::LIGHTING
