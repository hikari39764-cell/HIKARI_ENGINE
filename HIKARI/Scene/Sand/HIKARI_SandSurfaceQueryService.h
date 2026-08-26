#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"
#include "Scene/Terrain/HIKARI_TerrainSnapshotService.h"

namespace HIKARI::SAND {

    // 1 chunk 分の可変状態。System が main-thread fixed step でのみ書き換え、
    // query / renderer bridge は shared_ptr 越しに読む。
    // 高さは「基底(筆刷が彫った形状) + 変形(球の凹み)」を合成した最終値。
    struct SandChunkRuntimeState {
        TERRAIN::TerrainChunkKey key{};
        uint32_t authoredCells = TERRAIN::kTerrainChunkCells;
        uint32_t cells = TERRAIN::kTerrainChunkCells;
        SandSurfaceSettings settings{};
        // chunk 左下角の地形オブジェクトローカル XZ。
        MATH::Vec2 chunkOriginLocal{ 0.0f, 0.0f };
        float cellSize = 0.25f;
        float heightScale = 0.32f;
        float chunkWorldSize = 8.0f;

        std::vector<float> baseHeights{};    // (cells+1)^2
        // elastic/plastic を分離し、deformHeights は query/render 向けの
        // 合成 projection としてだけ保持する。
        std::vector<float> elasticHeights{}; // short-lived response
        std::vector<float> plasticHeights{}; // retained track
        std::vector<float> recoveryHold{};   // per vertex seconds
        std::vector<float> elasticScratch{}; // fixed-step 再利用 buffer
        std::vector<float> deformHeights{};  // mask 後の elastic + plastic
        std::vector<float> heights{};        // base + mask 後 deform
        std::vector<float> coverage{};       // visible solid coverage
        // layerWeights を runtime response grid へ投影した変形 gate。
        // authored / serialized data ではなく、SandSurfaceSystem が毎回再構築する。
        std::vector<float> deformableMask{};
        // responseGridActive は一度でも痕跡が付いたら play 中は維持する。
        // renderer はこれを境界に authored 解像度から response 解像度へ切り替える。
        bool responseGridActive = false;
        bool hasDeformation = false;
        // hasDeformation は保持中の塑性痕跡、needsRelaxation は System 内で
        // fixed tick 緩和を続ける短期応答を表す。古い足跡を残したまま
        // 高頻度処理から外し、immutable query snapshot はこの lifecycle を使わない。
        bool needsRelaxation = false;
        uint64_t stateRevision = 1u;
    };

    using SandChunkRuntimeStatePtr =
        std::shared_ptr<SandChunkRuntimeState>;
    using SandChunkRuntimeSnapshotPtr =
        std::shared_ptr<const SandChunkRuntimeState>;

    // TerrainSurface の層 preset と authored layerWeights から、runtime 解像度の
    // 変形 gate を作る。registry 未初期化や旧 document の layerPresetIds 欠落時は
    // sand/snow/dirt/grass/rock の固定順を使用する。
    bool IsSandLayerDeformable(
        const TERRAIN::TerrainSurface& surface,
        uint32_t layer) noexcept;
    void ProjectSandChunkDeformableMask(
        const TERRAIN::TerrainSurface& surface,
        const TERRAIN::TerrainChunk& authored,
        uint32_t runtimeCells,
        std::vector<float>& outMask);

    // base + response の最終高さを一箇所で定義する。runtime mask は query と
    // renderer bridge が読む final heights にだけ適用し、serialized terrain には入れない。
    float EvaluateSandChunkFinalHeight(
        float baseHeight,
        float rawDeformation,
        float deformableMask,
        bool deformationEnabled = true) noexcept;

    // 可変 simulation state から query / renderer が必要な projection だけを
    // 複製する。base/elastic/plastic/hold/scratch は main-thread owner に残し、
    // fixed tick ごとの snapshot 発行で不要な大配列を複製しない。
    SandChunkRuntimeSnapshotPtr MakeSandChunkRuntimeSnapshot(
        const SandChunkRuntimeState& source);

    struct SandSurfaceSnapshotEntry {
        RuntimeObjectHandle object{};
        SandChunkRuntimeSnapshotPtr chunk{};
        MATH::Mat4 localToWorld{};
        MATH::Mat4 worldToLocal{};
        MATH::Mat4 normalMatrix{};
    };

    struct SandSurfaceSnapshotChunkKey {
        uint64_t object = 0u;
        int32_t chunkX = 0;
        int32_t chunkZ = 0;

        bool operator==(
            const SandSurfaceSnapshotChunkKey&) const noexcept = default;
    };

    struct SandSurfaceSnapshotChunkKeyHash {
        std::size_t operator()(
            const SandSurfaceSnapshotChunkKey& key) const noexcept;
    };

    // lookup と entry を同じ immutable atomic snapshot で公開し、reader が
    // 古い chunk vector と新しい spatial index を混在させない。
    struct SandSurfaceSnapshotIndex {
        std::vector<std::size_t> objectEntryIndices{};
        std::unordered_map<
            SandSurfaceSnapshotChunkKey,
            std::size_t,
            SandSurfaceSnapshotChunkKeyHash> chunkEntryIndices{};
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
        bool SampleChunkLocalCoverage(
            const SandChunkRuntimeState& chunk,
            float localX,
            float localZ,
            float& outCoverage) const noexcept;
        // GPU と同じ shoreline sink を適用した最終地表高と coverage を返す。
        // 可視判定は kSandSurfaceVisibleCoverageThreshold を共有する。
        bool SampleChunkLocalSurface(
            const SandChunkRuntimeState& chunk,
            float localX,
            float localZ,
            float& outHeight,
            float& outCoverage) const noexcept;

        // object + chunkKey の不変 snapshot を返す。
        bool TryGetChunkState(
            RuntimeObjectHandle object,
            TERRAIN::TerrainChunkKey key,
            SandChunkRuntimeSnapshotPtr& outChunk) const noexcept;

        uint64_t GetSnapshotRevision() const noexcept;
        std::size_t GetSurfaceCount() const noexcept;
        void PublishSnapshot(
            std::vector<SandSurfaceSnapshotEntry> entries);
        void Clear();

    private:
        TERRAIN::TerrainSnapshotService<
            SandSurfaceSnapshotEntry,
            SandSurfaceSnapshotIndex>
            snapshotStore_{};
    };

} // namespace HIKARI::SAND
