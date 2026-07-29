#pragma once

#include <cstdint>

#include <d3d12.h>
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightGpuData.h"

namespace HIKARI {
    class Camera3D;
}

namespace HIKARI::RENDER3D::LIGHTING {

    inline constexpr uint32_t kMaximumLocalLightCount = 1024u;
    inline constexpr uint32_t kLocalLightClusterDepthSliceCount = 16u;
    inline constexpr uint32_t kMaximumLocalLightsPerCluster = 64u;
    inline constexpr uint32_t kMaximumLocalLightClusterCount = 65536u;
    inline constexpr uint32_t kMaximumLocalLightClusterIndexCount =
        1024u * 1024u;

    struct alignas(8) LocalLightClusterHeader {
        uint32_t offset = 0u;
        uint32_t count = 0u;
    };

    static_assert(sizeof(LocalLightClusterHeader) == 8u);

    struct LocalLightListBuildOutput {
        LocalLightGpuData* lights = nullptr;
        uint32_t lightCapacity = 0u;
        LocalLightClusterHeader* clusterHeaders = nullptr;
        uint32_t clusterHeaderCapacity = 0u;
        uint32_t* clusterIndices = nullptr;
        uint32_t clusterIndexCapacity = 0u;
    };

    struct LocalLightListView {
        uint32_t lightCount = 0u;
        uint32_t clusterCountX = 0u;
        uint32_t clusterCountY = 0u;
        uint32_t clusterCountZ = 0u;
        uint32_t tileSizePixels = 64u;
        uint32_t clusterCount = 0u;
        uint32_t clusterIndexCount = 0u;
        float nearDistance = 0.1f;
        float farDistance = 100.0f;
        uint32_t sourceCount = 0u;
        uint32_t uploadableCount = 0u;
        uint32_t rejectedCount = 0u;
        uint32_t overflowCount = 0u;
        uint32_t pointLightCount = 0u;
        uint32_t spotLightCount = 0u;
    };

    struct LocalLightListGpuBindings {
        D3D12_GPU_VIRTUAL_ADDRESS lights = 0u;
        D3D12_GPU_VIRTUAL_ADDRESS clusterHeaders = 0u;
        D3D12_GPU_VIRTUAL_ADDRESS clusterIndices = 0u;
        LocalLightListView view{};

        bool IsValid() const noexcept {
            return lights != 0u &&
                clusterHeaders != 0u &&
                clusterIndices != 0u;
        }
    };

    LocalLightListView BuildLocalLightList(
        const SceneLightingSet& lighting,
        const Camera3D& camera,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const LocalLightListBuildOutput& output);

    const SceneLightProvider* SelectDirectionalLight(
        const SceneLightingSet& lighting) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
