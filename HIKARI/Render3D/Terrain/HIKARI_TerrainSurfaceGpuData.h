#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceRenderData.h"

namespace HIKARI::RENDER3D::TERRAIN {

constexpr uint32_t kTerrainSurfaceGpuSchemaVersion = 2u;
constexpr uint32_t kTerrainSurfaceGpuLayerCountShift = 1u;
constexpr uint32_t kTerrainSurfaceGpuLayerCountMask = 0x7Fu << kTerrainSurfaceGpuLayerCountShift;
constexpr uint32_t kTerrainSurfaceGpuDeformationOffsetShift = 8u;
constexpr uint32_t kTerrainSurfaceGpuMaxDeformationOffset = 0x00FFFFFFu;
constexpr uint32_t kTerrainSurfaceGpuDeformationOffsetMask = kTerrainSurfaceGpuMaxDeformationOffset
                                                             << kTerrainSurfaceGpuDeformationOffsetShift;

enum class TerrainSurfaceGpuFlags : uint32_t {
    None = 0,
    Enabled = 1u << 0,
};

// HLSL の HikariSandSurfaceGpuData と同一の sparse record layout。
// index 0 は全 feature buffer で必須の neutral sentinel とする。
struct alignas(16) TerrainSurfaceGpuData {
    uint32_t schemaVersion = 0u;
    uint32_t flags = 0u;
    uint32_t gridResolution = 0u;
    // 高さ配列内の tile offset。sentinel では有効 record 数になる。
    uint32_t heightOffset = 0u;

    MATH::Vec4 extentsScale{0.0f, 0.0f, 0.0f, 0.0f};
    MATH::Vec4 styleLight{0.0f, 0.0f, 0.0f, 0.0f};
    MATH::Vec4 styleDark{0.0f, 0.0f, 0.0f, 0.0f};
    MATH::Vec4 stylePit{0.0f, 0.0f, 0.0f, 0.0f};
    MATH::Vec4 styleWet{0.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(TerrainSurfaceGpuData) == 96u);
static_assert(alignof(TerrainSurfaceGpuData) == 16u);
static_assert(offsetof(TerrainSurfaceGpuData, schemaVersion) == 0u);
static_assert(offsetof(TerrainSurfaceGpuData, heightOffset) == 12u);
static_assert(offsetof(TerrainSurfaceGpuData, extentsScale) == 16u);
static_assert(offsetof(TerrainSurfaceGpuData, styleLight) == 32u);
static_assert(offsetof(TerrainSurfaceGpuData, styleDark) == 48u);
static_assert(offsetof(TerrainSurfaceGpuData, stylePit) == 64u);
static_assert(offsetof(TerrainSurfaceGpuData, styleWet) == 80u);
static_assert((static_cast<uint32_t>(TerrainSurfaceGpuFlags::Enabled) & kTerrainSurfaceGpuLayerCountMask) == 0u);
static_assert((kTerrainSurfaceGpuLayerCountMask & kTerrainSurfaceGpuDeformationOffsetMask) == 0u);
static_assert(kTerrainSurfaceMaxHeightCells - 1u <= kTerrainSurfaceGpuMaxDeformationOffset);

TerrainSurfaceGpuData BuildTerrainSurfaceGpuDataSentinel() noexcept;
TerrainSurfaceGpuData BuildTerrainSurfaceGpuData(const TerrainSurfaceRenderData& source, uint32_t heightOffset,
                                                 uint32_t deformationOffset, uint32_t layerWeightsOffset,
                                                 uint32_t coverageOffset, uint32_t layerTintsOffset = 0u) noexcept;

} // namespace HIKARI::RENDER3D::TERRAIN
