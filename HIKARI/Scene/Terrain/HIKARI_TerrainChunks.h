#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::TERRAIN {

    // 既定の chunk 一辺のセル数。1 chunk = (kChunkCells+1)^2 頂点。
    constexpr uint32_t kTerrainChunkCells = 32u;
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
        bool operator==(const TerrainChunkKey& rhs) const noexcept {
            return x == rhs.x && z == rhs.z;
        }
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
    };

    inline std::size_t TerrainChunkVertexCount(uint32_t cells) noexcept {
        const uint32_t stride = cells + 1u;
        return static_cast<std::size_t>(stride) * stride;
    }

    inline bool TerrainChunkValid(const TerrainChunk& chunk) noexcept {
        if (chunk.cells == 0u ||
            chunk.layerCount == 0u ||
            chunk.layerCount > kTerrainChunkMaxLayers) {
            return false;
        }
        const std::size_t count = TerrainChunkVertexCount(chunk.cells);
        return chunk.heights.size() == count &&
            chunk.coverage.size() == count &&
            chunk.layerWeights.size() ==
                count * static_cast<std::size_t>(chunk.layerCount);
    }

    inline TerrainChunk MakeTerrainChunk(
        uint32_t cells = kTerrainChunkCells,
        uint32_t layerCount = 1u) {
        TerrainChunk chunk{};
        chunk.cells = (std::max)(cells, 1u);
        chunk.layerCount = (std::clamp)(
            layerCount, 1u, kTerrainChunkMaxLayers);
        const std::size_t count = TerrainChunkVertexCount(chunk.cells);
        chunk.heights.assign(count, 0.0f);
        chunk.coverage.assign(count, 1.0f); // 既定は実体（消しゴムで 0 へ）
        chunk.layerWeights.assign(
            count * static_cast<std::size_t>(chunk.layerCount), 0.0f);
        // 基底層を 1 にしておくと、ペイント時の正規化が安定する。
        for (std::size_t cell = 0u; cell < count; ++cell) {
            chunk.layerWeights[cell] = 1.0f;
        }
        return chunk;
    }

    // ワールド XZ -> chunk キー。負座標でも正しく床落ちする。
    inline TerrainChunkKey TerrainChunkKeyFromWorld(
        float worldX,
        float worldZ,
        float chunkWorldSize) noexcept {
        const float inv = 1.0f / (std::max)(chunkWorldSize, 1.0e-4f);
        return TerrainChunkKey{
            static_cast<int32_t>(std::floor(worldX * inv)),
            static_cast<int32_t>(std::floor(worldZ * inv))
        };
    }

    // 疎 chunk テーブル。キー順走査で安定した draw / 衝突順を保つ。
    using TerrainChunkMap = std::map<TerrainChunkKey, TerrainChunk>;

} // namespace HIKARI::TERRAIN
