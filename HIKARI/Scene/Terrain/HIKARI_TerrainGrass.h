#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"

namespace HIKARI::TERRAIN {

// 草层 authored settings。草几何不是场景对象；这个值型设置只描述
// 草层投影的可重复输入，实际 blade/clump 由 Render3D 消费 placement。
constexpr uint32_t kTerrainGrassVariantCount = 4u;
// SurfaceGpuSceneFrameBuffer 的默认 resident capacity 是 8192。每个
// grass clump 会投影成一个 SceneRenderObject/surface，因此这里不能
// 把草地当作无限 instancing：1024 是单个 terrain 的硬上限，2048 是
// 所有 terrain 共享的投影预算，给普通 scene object 和多 terrain 留出
// 余量；不要借此抬高 renderer 的全局容量。
constexpr uint32_t kTerrainGrassMaxClumps = 1024u;
constexpr uint32_t kTerrainGrassMaxProjectedInstances = 2048u;

struct TerrainGrassSettings {
    bool enabled = true;
    // 每个 terrain cell 至多产生一个 clump，最终概率还会乘以
    // grass layer weight 和 terrain coverage。
    float density = 0.42f;
    float bladeHeight = 0.34f;
    float bladeWidth = 0.028f;
    float heightVariation = 0.24f;
    float layerWeightThreshold = 0.35f;
    float maxSlopeDegrees = 42.0f;
    // World-space maximum tip displacement in metres.
    float windStrength = 0.055f;
    float windSpeed = 1.25f;
    float spatialFrequency = 0.85f;
    uint64_t seed = 0x47524153535F5631ull; // "GRASS_V1"
    uint32_t maxClumps = kTerrainGrassMaxClumps / 2u;

    bool operator==(const TerrainGrassSettings&) const = default;
};

// 保存・読み取り・検査器・説明の項目表。範囲は SanitizeTerrainGrassSettings の丸めと同じ。
inline constexpr auto kTerrainGrassFields = [](auto&& field) {
    using S = TerrainGrassSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Help{"Enable the deterministic grass placement projection."});
    field("density", &S::density, Range{0.0, 1.0}, Label{"Density"},
          Help{"Probability of a grass clump per terrain cell."});
    field("bladeHeight", &S::bladeHeight, Range{0.005, 10.0}, Label{"Blade Height"},
          Help{"Base height of each generated blade."});
    field("bladeWidth", &S::bladeWidth, Range{0.001, 2.0}, Label{"Blade Width"},
          Help{"Base width of each generated blade."});
    field("layerWeightThreshold", &S::layerWeightThreshold, Range{0.0, 1.0}, Label{"Layer Weight Threshold"},
          Help{"Minimum grass layer weight required for placement."});
    field("maxSlopeDegrees", &S::maxSlopeDegrees, Range{0.0, 90.0}, Label{"Max Slope"},
          Help{"Maximum terrain slope that can receive grass."});
    field("windStrength", &S::windStrength, Range{0.0, 0.5}, Label{"Wind Strength"},
          Help{"Amplitude of the animated foliage wind."});
    field("windSpeed", &S::windSpeed, Range{0.0, 20.0}, Label{"Wind Speed"},
          Help{"Speed of the animated foliage wind."});
    field("spatialFrequency", &S::spatialFrequency, Range{0.0, 20.0}, Label{"Spatial Frequency"},
          Help{"World-space frequency of the wind field."});
    field("heightVariation", &S::heightVariation, Range{0.0, 1.0}, Label{"Height Variation"},
          Help{"Per-clump height variation multiplier."});
    field("seed", &S::seed, Label{"Grass Seed"}, Help{"Stable seed used to generate grass placements."});
    field("maxClumps", &S::maxClumps, Range{0.0, static_cast<double>(kTerrainGrassMaxClumps)}, Label{"Max Clumps"},
          Help{"Upper bound for generated grass clumps."});
};

// Placement 是每次 terrain snapshot 生成的非持久值；它不拥有
// GameObject、mesh、material 或 GPU resource，也不进入 scene JSON。
struct TerrainGrassPlacement {
    TerrainChunkKey chunk{};
    uint32_t cellX = 0u;
    uint32_t cellZ = 0u;
    uint32_t variantIndex = 0u;
    MATH::Vec3 localPosition{};
    float heightScale = 1.0f;
    float widthScale = 1.0f;
    float yawDegrees = 0.0f;
    uint64_t stableHash = 0u;
};

struct TerrainGrassPlacementSnapshot {
    uint64_t seed = 0u;
    bool enabled = false;
    uint32_t grassLayerIndex = kTerrainChunkMaxLayers;
    std::vector<TerrainGrassPlacement> placements{};
};

// 項目表の範囲へ丸める（非有限値は既定値）。
TerrainGrassSettings SanitizeTerrainGrassSettings(TerrainGrassSettings value) noexcept;

bool IsTerrainGrassPlacementValid(const TerrainGrassPlacement& placement) noexcept;

// 由 layerPresetIds/basePresetId 解析第一个 Grass material layer。
// 若场景没有 grass layer，snapshot.enabled 为 false 且 placements 为空。
// chunk/cell 走稳定 map/索引顺序；超过预算时保留 stableHash 最小的
// 固定数量候选，避免大地形只在 map 前部 chunk 长草；一个 cell 最多
// 一个 clump，且总数永远不超过 settings.maxClumps。
TerrainGrassPlacementSnapshot BuildTerrainGrassPlacementSnapshot(const TerrainGrassSettings& settings,
                                                                 uint32_t chunkCells, float chunkCellSize,
                                                                 const TerrainChunkMap& chunks,
                                                                 const std::vector<std::string>& layerPresetIds,
                                                                 std::string_view basePresetId);

} // namespace HIKARI::TERRAIN
