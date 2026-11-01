#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceRenderData.h"

namespace HIKARI::RENDER3D::TERRAIN {

constexpr uint32_t kTerrainSurfaceGpuSchemaVersion = 3u;

enum class TerrainSurfaceGpuFlags : uint32_t {
    None = 0,
    Enabled = 1u << 0,
};

// float pool 内の各 field の先頭 offset。0 は neutral (未常駐) を表す。
struct TerrainSurfaceFieldOffsets {
    uint32_t height = 0u;
    uint32_t slope = 0u;
    uint32_t coverage = 0u;
    uint32_t deformation = 0u;
    uint32_t layerWeights = 0u;
    uint32_t layerStyles = 0u;

    bool operator==(const TerrainSurfaceFieldOffsets&) const = default;
};

// HLSL の HikariTerrainSurfaceGpuData と同一の sparse record layout。
// index 0 は全 feature buffer で必須の neutral sentinel とする。
struct alignas(16) TerrainSurfaceGpuData {
    uint32_t schemaVersion = 0u;
    uint32_t flags = 0u;
    uint32_t gridResolution = 0u;
    // sentinel では有効 record 数になる。
    uint32_t layerCount = 0u;

    uint32_t heightOffset = 0u;
    uint32_t slopeOffset = 0u;
    uint32_t coverageOffset = 0u;
    uint32_t deformationOffset = 0u;

    uint32_t layerWeightsOffset = 0u;
    uint32_t layerStylesOffset = 0u;
    uint32_t reserved0 = 0u;
    uint32_t reserved1 = 0u;

    MATH::Vec4 extentsScale{0.0f, 0.0f, 0.0f, 0.0f}; // extentX, extentZ, heightScale, rimStrength
    MATH::Vec4 styleLight{0.0f, 0.0f, 0.0f, 0.0f};   // lightTint.rgb, sparkleStrength
    MATH::Vec4 styleDark{0.0f, 0.0f, 0.0f, 0.0f};    // darkTint.rgb, rippleAmount
    MATH::Vec4 styleShade{0.0f, 0.0f, 0.0f, 0.0f};   // pitDarkening, toonBandHardness, toonBandCount, wetness
    MATH::Vec4 envelope{0.0f, 0.0f, 0.0f, 0.0f};     // boundsMinY, boundsMaxY, edgeDepth, reserved
};

static_assert(sizeof(TerrainSurfaceGpuData) == 128u);
static_assert(alignof(TerrainSurfaceGpuData) == 16u);
static_assert(offsetof(TerrainSurfaceGpuData, heightOffset) == 16u);
static_assert(offsetof(TerrainSurfaceGpuData, layerWeightsOffset) == 32u);
static_assert(offsetof(TerrainSurfaceGpuData, extentsScale) == 48u);
static_assert(offsetof(TerrainSurfaceGpuData, styleShade) == 96u);
static_assert(offsetof(TerrainSurfaceGpuData, envelope) == 112u);

TerrainSurfaceGpuData BuildTerrainSurfaceGpuDataSentinel(uint32_t recordCount) noexcept;
TerrainSurfaceGpuData BuildTerrainSurfaceGpuData(const TerrainSurfaceRenderData& source,
                                                 const TerrainSurfaceFieldOffsets& offsets) noexcept;

} // namespace HIKARI::RENDER3D::TERRAIN
