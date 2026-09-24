#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::TERRAIN {

// 既定の chunk 一辺のセル数。1 chunk = (kChunkCells+1)^2 頂点。
constexpr uint32_t kTerrainChunkCells = 32u;
// Runtime/editor の height field と草配置が一度に扱う chunk 解像度の
// 上限。serialized uint32_t をそのまま乗算しない。
constexpr uint32_t kTerrainChunkMaxCells = 512u;
constexpr uint32_t kTerrainChunkMaxLayers = 8u;

// ワールド XZ を chunk 座標へ割り当てる整数キー。
struct TerrainChunkKey {
    int32_t x = 0;
    int32_t z = 0;

    bool operator<(const TerrainChunkKey& rhs) const noexcept {
        if (x != rhs.x) {
            return x < rhs.x;
        }
        return z < rhs.z;
    }
    bool operator==(const TerrainChunkKey& rhs) const noexcept { return x == rhs.x && z == rhs.z; }
};

// 1 chunk のローカルデータ。セルピッチはコンポーネント側で統一する。
// heights / coverage は (cells+1)^2、layerWeights は layerCount 層分の
// layer-major 配列。着色計算はシェーダー側が層 tint を別途参照する。
struct TerrainChunk {
    uint32_t cells = kTerrainChunkCells;
    uint32_t layerCount = 1u;
    std::vector<float> heights{};      // (cells+1)^2
    std::vector<float> coverage{};     // (cells+1)^2, 0..1
    std::vector<float> layerWeights{}; // layerCount * (cells+1)^2

    bool operator==(const TerrainChunk&) const = default;
};

inline bool TerrainChunkCellCountIsSafe(uint32_t cells) noexcept {
    return cells >= 1u && cells <= kTerrainChunkMaxCells;
}

inline bool TryGetTerrainChunkVertexCount(uint32_t cells, std::size_t& outCount) noexcept {
    outCount = 0u;
    if (!TerrainChunkCellCountIsSafe(cells)) {
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(cells) + 1u;
    if (stride > (std::numeric_limits<std::size_t>::max)() / stride) {
        return false;
    }
    outCount = stride * stride;
    return true;
}

inline std::size_t TerrainChunkVertexCount(uint32_t cells) noexcept {
    std::size_t count = 0u;
    (void)TryGetTerrainChunkVertexCount(cells, count);
    return count;
}

inline bool TerrainChunkValid(const TerrainChunk& chunk) noexcept {
    if (!TerrainChunkCellCountIsSafe(chunk.cells) || chunk.layerCount == 0u ||
        chunk.layerCount > kTerrainChunkMaxLayers) {
        return false;
    }
    std::size_t count = 0u;
    if (!TryGetTerrainChunkVertexCount(chunk.cells, count) ||
        count > (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(chunk.layerCount)) {
        return false;
    }
    return chunk.heights.size() == count && chunk.coverage.size() == count &&
           chunk.layerWeights.size() == count * static_cast<std::size_t>(chunk.layerCount);
}

inline TerrainChunk MakeTerrainChunk(uint32_t cells = kTerrainChunkCells, uint32_t layerCount = 1u) {
    TerrainChunk chunk{};
    chunk.cells = (std::clamp)(cells, 1u, kTerrainChunkMaxCells);
    chunk.layerCount = (std::clamp)(layerCount, 1u, kTerrainChunkMaxLayers);
    const std::size_t count = TerrainChunkVertexCount(chunk.cells);
    chunk.heights.assign(count, 0.0f);
    chunk.coverage.assign(count, 1.0f); // 既定は実体（消しゴムで 0 へ）
    chunk.layerWeights.assign(count * static_cast<std::size_t>(chunk.layerCount), 0.0f);
    // 基底層を 1 にしておくと、ペイント時の正規化が安定する。
    for (std::size_t cell = 0u; cell < count; ++cell) {
        chunk.layerWeights[cell] = 1.0f;
    }
    return chunk;
}

// 新規 chunk がペイント対象外の基底層を誤って持たないよう、層 major
// 配列の拡張規則を chunk 所有者へ集約する。全 authoring 入口は同じ
// 拡張 / fill semantics を通る。
inline bool EnsureTerrainChunkLayerCount(TerrainChunk& chunk, uint32_t requiredLayerCount) {
    if (requiredLayerCount == 0u || requiredLayerCount > kTerrainChunkMaxLayers) {
        return false;
    }
    if (requiredLayerCount <= chunk.layerCount) {
        return TerrainChunkValid(chunk);
    }
    if (!TerrainChunkValid(chunk)) {
        return false;
    }
    const std::size_t vertexCount = TerrainChunkVertexCount(chunk.cells);
    const std::size_t layerCount = static_cast<std::size_t>(requiredLayerCount);
    if (vertexCount == 0u || vertexCount > (std::numeric_limits<std::size_t>::max)() / layerCount) {
        return false;
    }
    std::vector<float> expanded(vertexCount * layerCount, 0.0f);
    for (uint32_t layer = 0u; layer < chunk.layerCount; ++layer) {
        const std::size_t sourceOffset = static_cast<std::size_t>(layer) * vertexCount;
        std::copy_n(chunk.layerWeights.begin() + static_cast<std::ptrdiff_t>(sourceOffset), vertexCount,
                    expanded.begin() + static_cast<std::ptrdiff_t>(sourceOffset));
    }
    chunk.layerCount = requiredLayerCount;
    chunk.layerWeights = std::move(expanded);
    return true;
}

// 高さ場と coverage を保持したまま全頂点を一つの材質へ揃える。
// Editor の明示的な「地形全体を fill」操作のデータ層である。
inline bool FillTerrainChunkLayer(TerrainChunk& chunk, uint32_t targetLayer) {
    if (!TerrainChunkCellCountIsSafe(chunk.cells) || targetLayer >= kTerrainChunkMaxLayers) {
        return false;
    }
    if (!TerrainChunkValid(chunk) && (chunk.layerCount == 0u || chunk.layerCount > kTerrainChunkMaxLayers)) {
        return false;
    }
    if (!EnsureTerrainChunkLayerCount(chunk, targetLayer + 1u)) {
        return false;
    }
    const std::size_t vertexCount = TerrainChunkVertexCount(chunk.cells);
    bool changed = false;
    for (std::size_t vertex = 0u; vertex < vertexCount; ++vertex) {
        for (uint32_t layer = 0u; layer < chunk.layerCount; ++layer) {
            const float desired = layer == targetLayer ? 1.0f : 0.0f;
            float& weight = chunk.layerWeights[static_cast<std::size_t>(layer) * vertexCount + vertex];
            changed = changed || std::fabs(weight - desired) > 1.0e-7f;
            weight = desired;
        }
    }
    return changed;
}

enum class TerrainChunkEdge : uint8_t {
    NegativeX = 0,
    PositiveX = 1,
    NegativeZ = 2,
    PositiveZ = 3,
};

// 有効な隣接 chunk の材質 edge を新規 chunk 全体へ継承する。高さと
// coverage は呼び出し側の所有とし、layer weight だけを供給する。
// coverage を変更しないため、四角い chunk の輪郭は露出しない。
inline bool CopyTerrainChunkEdgeToAll(const TerrainChunk& source, TerrainChunk& target, TerrainChunkEdge sourceEdge) {
    if (!TerrainChunkValid(source) || !TerrainChunkValid(target) || source.layerCount == 0u) {
        return false;
    }
    if (!EnsureTerrainChunkLayerCount(target, source.layerCount)) {
        return false;
    }
    const std::size_t sourceVertexCount = TerrainChunkVertexCount(source.cells);
    const std::size_t targetVertexCount = TerrainChunkVertexCount(target.cells);
    const uint32_t sourceStride = source.cells + 1u;
    const uint32_t targetStride = target.cells + 1u;
    const auto sourceIndexFor = [&](uint32_t coordinate) {
        const float normalized =
            target.cells == 0u ? 0.0f : static_cast<float>(coordinate) / static_cast<float>(target.cells);
        return static_cast<uint32_t>(std::clamp(std::lround(normalized * static_cast<float>(source.cells)), 0l,
                                                static_cast<long>(source.cells)));
    };
    bool copied = false;
    for (uint32_t z = 0u; z <= target.cells; ++z) {
        for (uint32_t x = 0u; x <= target.cells; ++x) {
            const uint32_t sourceCoordinate =
                (sourceEdge == TerrainChunkEdge::NegativeX || sourceEdge == TerrainChunkEdge::PositiveX)
                    ? sourceIndexFor(z)
                    : sourceIndexFor(x);
            const uint32_t sourceX = sourceEdge == TerrainChunkEdge::NegativeX   ? 0u
                                     : sourceEdge == TerrainChunkEdge::PositiveX ? source.cells
                                                                                 : sourceCoordinate;
            const uint32_t sourceZ = sourceEdge == TerrainChunkEdge::NegativeZ   ? 0u
                                     : sourceEdge == TerrainChunkEdge::PositiveZ ? source.cells
                                                                                 : sourceCoordinate;
            const std::size_t sourceVertex = static_cast<std::size_t>(sourceZ) * sourceStride + sourceX;
            const std::size_t targetVertex = static_cast<std::size_t>(z) * targetStride + x;
            for (uint32_t layer = 0u; layer < target.layerCount; ++layer) {
                const float value =
                    layer < source.layerCount
                        ? source.layerWeights[static_cast<std::size_t>(layer) * sourceVertexCount + sourceVertex]
                        : 0.0f;
                target.layerWeights[static_cast<std::size_t>(layer) * targetVertexCount + targetVertex] = value;
                copied = copied || value > 1.0e-7f;
            }
        }
    }
    return copied;
}

// ワールド XZ -> chunk キー。負座標でも正しく床落ちする。
inline TerrainChunkKey TerrainChunkKeyFromWorld(float worldX, float worldZ, float chunkWorldSize) noexcept {
    const float inv = 1.0f / (std::max)(chunkWorldSize, 1.0e-4f);
    return TerrainChunkKey{static_cast<int32_t>(std::floor(worldX * inv)),
                           static_cast<int32_t>(std::floor(worldZ * inv))};
}

// 疎 chunk テーブル。キー順走査で安定した draw / 衝突順を保つ。
using TerrainChunkMap = std::map<TerrainChunkKey, TerrainChunk>;

// 共有 lattice 頂点の authoritative source。chunk の +X / +Z 端は
// 隣接する正方向 chunk が存在すればそちらを優先する。これは従来の
// owner-wide terrain projection と SampleLocalRaw が使う探索順と同じで、
// 古い scene に残る seam 値の不一致を描画・衝突へ露出させない。
struct TerrainCanonicalVertexSource {
    TerrainChunkKey key{};
    const TerrainChunk* chunk = nullptr;
    uint32_t localX = 0u;
    uint32_t localZ = 0u;
};

inline bool TryResolveTerrainCanonicalVertex(const TerrainChunkMap& chunks, const TerrainChunkKey& sourceKey,
                                             uint32_t sourceX, uint32_t sourceZ,
                                             TerrainCanonicalVertexSource& outSource) noexcept {
    outSource = {};
    const auto source = chunks.find(sourceKey);
    if (source == chunks.end() || !TerrainChunkValid(source->second) || sourceX > source->second.cells ||
        sourceZ > source->second.cells) {
        return false;
    }

    const int64_t cells = static_cast<int64_t>(source->second.cells);
    const int64_t globalX = static_cast<int64_t>(sourceKey.x) * cells + sourceX;
    const int64_t globalZ = static_cast<int64_t>(sourceKey.z) * cells + sourceZ;
    const auto floorDiv = [cells](int64_t value) noexcept {
        int64_t quotient = value / cells;
        if (value % cells < 0) {
            --quotient;
        }
        return quotient;
    };
    const int64_t baseX = floorDiv(globalX);
    const int64_t baseZ = floorDiv(globalZ);
    if (baseX < (std::numeric_limits<int32_t>::min)() || baseX > (std::numeric_limits<int32_t>::max)() ||
        baseZ < (std::numeric_limits<int32_t>::min)() || baseZ > (std::numeric_limits<int32_t>::max)()) {
        return false;
    }

    constexpr int32_t offsets[2] = {0, -1};
    for (const int32_t dz : offsets) {
        for (const int32_t dx : offsets) {
            const int64_t candidateX = baseX + dx;
            const int64_t candidateZ = baseZ + dz;
            if (candidateX < (std::numeric_limits<int32_t>::min)() ||
                candidateX > (std::numeric_limits<int32_t>::max)() ||
                candidateZ < (std::numeric_limits<int32_t>::min)() ||
                candidateZ > (std::numeric_limits<int32_t>::max)()) {
                continue;
            }
            const TerrainChunkKey candidateKey{
                static_cast<int32_t>(candidateX),
                static_cast<int32_t>(candidateZ),
            };
            const auto candidate = chunks.find(candidateKey);
            if (candidate == chunks.end() || !TerrainChunkValid(candidate->second) ||
                candidate->second.cells != source->second.cells) {
                continue;
            }
            const int64_t localX = globalX - candidateX * cells;
            const int64_t localZ = globalZ - candidateZ * cells;
            if (localX < 0 || localX > cells || localZ < 0 || localZ > cells) {
                continue;
            }
            outSource.key = candidateKey;
            outSource.chunk = &candidate->second;
            outSource.localX = static_cast<uint32_t>(localX);
            outSource.localZ = static_cast<uint32_t>(localZ);
            return true;
        }
    }
    return false;
}

} // namespace HIKARI::TERRAIN
