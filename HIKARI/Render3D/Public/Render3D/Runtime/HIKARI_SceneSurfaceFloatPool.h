#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuData.h"

namespace HIKARI::RENDER3D::RUNTIME {

// gSurfaceFeatureFloats は全地形 field と水面 coverage が共有する。
// offset=0/gridResolution=1 を安全な可視 fallback にするため 4 zero を予約する。
constexpr std::size_t kSceneSurfaceNeutralFloatCount = 4u;
// GPU が過去 version から差分を追えるだけの履歴。frame slot 数より多く保つ。
constexpr std::size_t kSceneSurfaceFloatPoolHistoryCapacity = 8u;

// float pool 内の連続範囲 [first, first + count)。
struct SceneSurfaceFloatRange {
    uint32_t first = 0u;
    uint32_t count = 0u;
};

// baseVersion から version へ進んだ時に書き換えた範囲。
struct SceneSurfaceFloatPoolDelta {
    uint64_t baseVersion = 0u;
    uint64_t version = 0u;
    std::vector<SceneSurfaceFloatRange> ranges{};
};

// GPU upload へ渡す float pool の現在値と差分履歴。identity が変われば
// version 系列も別物として扱い、差分を使わず全量転送する。
struct SurfaceFeatureFloatPoolSource {
    const std::vector<float>* values = nullptr;
    uint64_t identity = 0u;
    uint64_t version = 0u;
    std::span<const SceneSurfaceFloatPoolDelta> history{};
};

// baseVersion から targetVersion までの変更範囲を集める。履歴が途切れていれば false。
bool CollectSceneSurfaceFloatRangesSince(std::span<const SceneSurfaceFloatPoolDelta> history, uint64_t baseVersion,
                                         uint64_t targetVersion, std::vector<SceneSurfaceFloatRange>& outRanges);

// 地形 core: 高さ・勾配・coverage・layer weight・layer style。atomic に収容する。
struct SceneSurfaceTerrainFloatFields {
    uint32_t gridResolution = 1u;
    uint32_t layerCount = 1u;
    const TERRAIN::TerrainSurfaceProjectedFields* fields = nullptr;
    const std::vector<TERRAIN::TerrainSurfaceLayerRenderStyle>* layerStyles = nullptr;
};

struct SceneSurfaceTerrainFloatAllocation {
    bool resident = false;
    uint32_t gridResolution = 1u;
    uint32_t layerCount = 1u;
    TERRAIN::TerrainSurfaceFieldOffsets offsets{};
};

// 1 頂点あたりの core float 数 (高さ + 勾配 + coverage + 詰めた layer weight)。
uint32_t TerrainSurfaceCoreFloatsPerVertex(uint32_t layerCount) noexcept;

// Scene surface float pool 唯一の deterministic first-fit owner。
// terrain core は atomic に収容し、optional field は offset zero へ縮退する。
class SceneSurfaceFloatPoolPacker final {
public:
    SceneSurfaceFloatPoolPacker(std::vector<float>& destination, std::size_t capacity);

    std::size_t GetRemaining() const noexcept;

    SceneSurfaceTerrainFloatAllocation PackTerrainCore(const SceneSurfaceTerrainFloatFields& source);
    bool TryPackSquareField(const std::vector<float>& values, uint32_t gridResolution, uint32_t& outOffset);
    bool TryPackSquareFieldResampled(const std::vector<float>& values, uint32_t sourceGridResolution,
                                     uint32_t targetGridResolution, uint32_t& outOffset);

private:
    bool IsUsable() const noexcept;
    uint32_t CurrentOffset() const noexcept;

    std::vector<float>& destination_;
    std::size_t capacity_ = 0u;
};

// 既に常駐している地形の行 [minZ, maxZ] を、同じ offset のまま書き戻す。
// packed grid が source grid と一致する時だけ使える。書いた範囲を outRanges へ足す。
bool PatchTerrainSurfaceRows(std::vector<float>& pool, const SceneSurfaceTerrainFloatAllocation& allocation,
                             const TERRAIN::TerrainSurfaceProjectedFields& fields, uint32_t minZ, uint32_t maxZ,
                             std::vector<SceneSurfaceFloatRange>& outRanges);

std::size_t ResolveSquareCellCount(uint32_t gridResolution) noexcept;

} // namespace HIKARI::RENDER3D::RUNTIME
