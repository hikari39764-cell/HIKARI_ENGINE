#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainSnapshotService.h"

namespace HIKARI::SAND {

    // 1 chunk 分の可変状態。System が main-thread fixed step でのみ書き換え、
    // query / renderer bridge は shared_ptr 越しに読む。
    // 高さは「基底(筆刷が彫った形状) + 変形(球の凹み)」を合成した最終値。
    struct SandChunkRuntimeState {
        TERRAIN::TerrainChunkKey key{};
        uint32_t cells = TERRAIN::kTerrainChunkCells;
        SandSurfaceSettings settings{};
        // chunk 左下角のワールド XZ。
        MATH::Vec2 chunkOriginWS{ 0.0f, 0.0f };
        float cellSize = 0.25f;
        float heightScale = 0.32f;
        float chunkWorldSize = 8.0f;

        std::vector<float> baseHeights{};   // (cells+1)^2
        std::vector<float> deformHeights{}; // (cells+1)^2
        std::vector<float> heights{};       // base + deform
    };

    using SandChunkRuntimeStatePtr =
        std::shared_ptr<SandChunkRuntimeState>;

    struct SandSurfaceSnapshotEntry {
        RuntimeObjectHandle object{};
        SandChunkRuntimeStatePtr chunk{};
    };

    // ワールド空間で疎 chunk 化された地形への問い合わせ。
    // フォロワー・水面 bathymetry・描画 bridge が共有する。
    class SandSurfaceQueryService final {
    public:
        // ワールド座標から該当 chunk を定位し、最終高さと法線を返す。
        // chunk が無い場所では false。
        bool SampleWorldHeight(
            const MATH::Vec3& worldPosition,
            float& outHeight,
            MATH::Vec3& outNormal) const noexcept;

        // 指定 chunk 内のローカル高さを返す（描画 bridge 用）。
        bool SampleChunkLocalHeight(
            const SandChunkRuntimeState& chunk,
            float localX,
            float localZ,
            float& outHeight) const noexcept;

        // Renderer bridge 用。object + chunkKey の高さ配列を借用する。
        bool TryGetChunkHeights(
            RuntimeObjectHandle object,
            TERRAIN::TerrainChunkKey key,
            const std::vector<float>*& outHeights) const noexcept;

        // object + chunkKey の chunk 可変状態そのものを借用する。
        // 描画 bridge が chunk 原点・セルピッチを含めて参照するために使う。
        bool TryGetChunkState(
            RuntimeObjectHandle object,
            TERRAIN::TerrainChunkKey key,
            const SandChunkRuntimeState*& outChunk) const noexcept;

        uint64_t GetSnapshotRevision() const noexcept;
        std::size_t GetSurfaceCount() const noexcept;
        void PublishSnapshot(
            std::vector<SandSurfaceSnapshotEntry> entries);
        void Clear();

    private:
        TERRAIN::TerrainSnapshotService<SandSurfaceSnapshotEntry>
            snapshotStore_{};
    };

} // namespace HIKARI::SAND
