#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightmapAtlasLayout.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"

namespace HIKARI::TOOLS::BAKING::INTERNAL {

    struct DxrLightmapSurfaceTexel {
        MATH::Vec4 position{};
        MATH::Vec4 normal{};
    };

    struct DxrLightmapUvRasterization {
        std::vector<DxrLightmapSurfaceTexel> texels{};
        std::vector<uint8_t> validTexels{};
        std::vector<uint32_t> ownerIndices{};
        uint32_t coveredTexelCount = 0u;
        uint32_t overlapTexelCount = 0u;
        uint32_t uncoveredAllocationCount = 0u;
    };

    DxrLightmapUvRasterization RasterizeLightmapTarget(
        const RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportScene& scene,
        const RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportSurfaceTarget& target,
        uint32_t width,
        uint32_t height);

    bool WriteProgressiveLightmapDds(
        const std::filesystem::path& outputPath,
        uint32_t width,
        uint32_t height,
        uint32_t expectedSampleCount,
        const MATH::Vec4* accumulation,
        std::vector<uint8_t> validTexels,
        std::vector<uint32_t> ownerIndices,
        const std::vector<ASSETS::LIGHTING::
            LightmapAtlasAllocation>& allocations,
        std::string* outMessage);

} // namespace HIKARI::TOOLS::BAKING::INTERNAL
