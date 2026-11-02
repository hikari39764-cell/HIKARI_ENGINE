#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <span>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightmapAtlasLayout.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"
#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"

namespace HIKARI::TOOLS::BAKING::INTERNAL {

struct DxrLightmapSurfaceTexel {
    // w は atlas 一 texel の world-space footprint。ゼロは未被覆。
    MATH::Vec4 position{};
    MATH::Vec4 normal{};
};

// 最初の光線が裏面に当たった標本がこの割合を超える texel は形状に埋まっている（壁や床の中、交差の隙間）とみなし、
// 無効にして同じ allocation の周りの texel から埋める（Unity の Backface Tolerance と同じ考え方）。
inline constexpr float kLightmapBackfaceTolerance = 0.1f;

struct DxrLightmapUvRasterization {
    std::vector<DxrLightmapSurfaceTexel> texels{};
    std::vector<uint8_t> validTexels{};
    std::vector<uint32_t> ownerIndices{};
    uint32_t coveredTexelCount = 0u;
    uint32_t overlapTexelCount = 0u;
    uint32_t uncoveredAllocationCount = 0u;
};

DxrLightmapUvRasterization RasterizeLightmapTarget(
    const RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene& scene,
    const RENDER3D::LIGHTING::TRANSPORT::LightingTransportLightmapPage& target, uint32_t width, uint32_t height);

// 面に沿った降噪。variance は texel ごとの平均の輝度の分散（標本の分散 / 標本数）で、近傍との差をこの雑音の大きさと
// 比べて混ぜる量を決める（SVGF の à-trous と同じ考え方）。
bool FilterLightmapIrradiance(std::span<MATH::Vec4> pixels, std::span<float> variance,
                              std::span<const DxrLightmapSurfaceTexel> surfaces, std::span<const uint8_t> validTexels,
                              std::span<const uint32_t> ownerIndices,
                              std::span<const ASSETS::LIGHTING::LightmapAtlasAllocation> allocations, uint32_t width,
                              uint32_t height);

bool WriteProgressiveLightmapDds(const std::filesystem::path& outputPath, uint32_t width, uint32_t height,
                                 uint32_t expectedSampleCount, const DxrTransportAccumulation* accumulation,
                                 std::vector<uint8_t> validTexels, std::vector<uint32_t> ownerIndices,
                                 const std::vector<ASSETS::LIGHTING::LightmapAtlasAllocation>& allocations,
                                 std::string* outMessage, std::span<const DxrLightmapSurfaceTexel> surfaces = {});

} // namespace HIKARI::TOOLS::BAKING::INTERNAL
