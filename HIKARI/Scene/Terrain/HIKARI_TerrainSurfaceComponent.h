#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainChunkChangeJournal.h"
#include "Scene/Terrain/HIKARI_TerrainProcedural.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI {

// 統合地形地表コンポーネント。固体地形を「オブジェクトローカル XZ に整列した疎
// chunk 表」として保持する。筆刷が届いた場所に chunk が生え、1 つの
// コンポーネントが任意範囲の地形を担う。水面は独立コンポーネント。
//
// 着色は従来の SandSurface パイプラインを chunk ごとに再利用し、
// 見た目（アニメ調沙地）を変えない。物理・変形も chunk 単位で駆動する。
class TerrainSurfaceComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "TerrainSurfaceComponent"; }

    const TERRAIN::TerrainSurface& GetSurface() const noexcept;
    void SetSurface(const TERRAIN::TerrainSurface& surface);
    const TERRAIN::TerrainStylizationSettings& GetStylizationSettings() const noexcept;
    void SetStylizationSettings(const TERRAIN::TerrainStylizationSettings& settings) noexcept;
    const TERRAIN::TerrainGrassSettings& GetGrassSettings() const noexcept;
    void SetGrassSettings(const TERRAIN::TerrainGrassSettings& settings) noexcept;

    void ApplySolidPreset(std::string_view presetId);
    // 明示的な材質操作。既存 chunk の全頂点を対象層へ揃え、authored
    // height / coverage は保持する。
    bool FillLayer(std::string_view presetId);

    // ── ワールド空間ブラシ ────────────────────────────
    // 以降の編集 API はすべてワールド座標を取る。内部で chunk を
    // 定位・生成し、該当 chunk のローカル高さ場へスタンプする。
    TERRAIN::TerrainBrush GetBrush() const noexcept;
    void SetBrush(const TERRAIN::TerrainBrush& brush) noexcept;
    void SetBrushMode(TERRAIN::TerrainBrushMode mode);
    void SetBrushRadius(float radius);
    void SetDeformParams(const TERRAIN::DeformParams& deform);
    void SetShorelineParams(const TERRAIN::ShorelineParams& shoreline);

    // 起伏スカルプト。worldPos を中心に brush を 1 回適用する。
    // deltaSeconds: フレーム時間。strength は「毎秒の伸び」として
    // 扱い、dt を掛けてフレームレート非依存にする（成熟ツール相当）。
    bool SculptAtWorld(const MATH::Vec3& worldPos, float deltaSeconds = 1.0f / 60.0f);
    bool SculptAtLocal(const MATH::Vec3& localPos, float deltaSeconds = 1.0f / 60.0f);
    // A ramp is a discrete two-point authoring operation. Callers own the
    // surrounding geometry edit batch and one document transaction.
    bool ApplyRampAtLocal(const TERRAIN::TerrainRampSpec& spec);
    bool ApplyOpeningAtLocal(const TERRAIN::TerrainOpeningSpec& spec);
    // 層ペイント。presetId の層を worldPos 周辺へ塗る。
    // deltaSeconds=1 は単発 authoring command の従来強度を保つ。
    // 連続 stroke は実フレーム時間を渡して FPS 非依存にする。
    bool PaintLayerAtWorld(std::string_view presetId, const MATH::Vec3& worldPos, float deltaSeconds = 1.0f);
    bool PaintLayerAtLocal(std::string_view presetId, const MATH::Vec3& localPos, float deltaSeconds = 1.0f);
    // coverage（実体/穴）を worldPos 周辺へ塗る。target=1 で実体。
    bool PaintCoverageAtWorld(float target, const MATH::Vec3& worldPos, float deltaSeconds = 1.0f);
    bool PaintCoverageAtLocal(float target, const MATH::Vec3& localPos, float deltaSeconds = 1.0f);

    // Bake one deterministic procedural profile into the existing sparse
    // heightfield. Callers that need Undo must wrap the call in one
    // BeginGeometryEditBatch/EndGeometryEditBatch transaction.
    bool ApplyProceduralTerrain(const TERRAIN::TerrainProceduralSpec& spec,
                                TERRAIN::TerrainProceduralPreview& outPreview);

    void ResetTerrain();

    // Editor stroke 中は描画を即時更新しつつ、重い static collision の
    // revision は stroke 終了時に 1 回だけ commit する。
    void BeginGeometryEditBatch() noexcept;
    bool EndGeometryEditBatch() noexcept;
    // 直前の geometry edit batch が触れた chunk だけを、Undo/Redo 用の
    // before/after delta として返す。巨大な TerrainSurface 全体を stroke
    // ごとに複製しない。返却後は delta を破棄する。
    bool ConsumeGeometryEditDelta(nlohmann::json& outBeforeDelta, nlohmann::json& outAfterDelta);

    // ── 物理 / 変形 ───────────────────────────────────
    // ワールド XZ の地形高さをサンプルする。chunk が無い場所は false。
    bool SampleWorldHeight(const MATH::Vec3& worldPos, float& outHeight, MATH::Vec3& outNormal) const noexcept;
    bool SampleLocalHeight(const MATH::Vec3& localPos, float& outHeight, MATH::Vec3& outNormal) const noexcept;
    // レイマーチや筆刷プレビュー向け。法線の 4 近傍サンプルを省き、
    // 可視 edge を反映した高さだけを返す。
    bool SampleLocalSurfaceHeight(const MATH::Vec3& localPos, float& outHeight) const noexcept;
    float SampleLocalCoverage(const MATH::Vec3& localPos) const noexcept;
    bool RaycastWorld(const MATH::Vec3& rayOrigin, const MATH::Vec3& rayDirection, float maxDistance,
                      MATH::Vec3& outPosition, MATH::Vec3& outNormal,
                      bool allowBasePlaneFallback = true) const noexcept;

    uint32_t GetLayerCount() const noexcept;
    uint64_t GetRevision() const noexcept;
    uint64_t GetGeometryRevision() const noexcept;
    uint64_t GetCoverageRevision() const noexcept;
    uint64_t GetCollisionRevision() const noexcept;
    // Render bridge は内容配列を毎 frame hash せず、この authored chunk
    // revision で差分を判定する。存在しない chunk は 0。
    uint64_t GetChunkRevision(const TERRAIN::TerrainChunkKey& key) const noexcept;
    uint64_t GetChunkChangeRevision() const noexcept;
    // Returns false when the requested interval was evicted or contains
    // a topology/global-settings event that requires a full owner sync.
    bool TryGetChangedChunkKeys(uint64_t sinceRevision, std::vector<TERRAIN::TerrainChunkKey>& outKeys) const;

    // chunk 走査用。描画・衝突・変形システムがここから chunk を得る。
    const TERRAIN::TerrainChunkMap& GetChunks() const noexcept;
    float GetChunkWorldSize() const noexcept;
    uint32_t GetChunkCells() const noexcept;

    // authored chunk / height / layer weight から毎回再構築する deterministic
    // snapshot。配置は Scene の GameObject や serialized transform ではない。
    TERRAIN::TerrainGrassPlacementSnapshot BuildGrassPlacementSnapshot() const;

    // 高さ場はオブジェクトローカル空間。ブラシ・変形・物理は
    // この変換でローカル座標へそろえ、描画と一致させる。
    MATH::Vec3 WorldToLocal(const MATH::Vec3& worldPos) const noexcept;
    MATH::Vec3 LocalToWorld(const MATH::Vec3& localPos) const noexcept;
    MATH::Vec3 WorldNormalToLocal(const MATH::Vec3& worldNormal) const noexcept;

    // Workspace の軽量 authoring transaction 用。巨大な chunk 配列を
    // 複製せず、preset / brush / edge / deform 設定だけを直列化する。
    void SerializeSettings(nlohmann::json& out) const;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;
    // Inspector と地形ワークスペースで設定の意味・範囲・setter を共有する。
    bool BuildSurfaceSettingsInspector(IInspectorBuilder& builder, bool expandGrass = false);

private:
    // Runtime document rebuild は既に所有している大きな chunk payload を
    // move し、公開 const& API だけが必要な 1 回の copy を担当する。
    void SetSurfaceOwned(TERRAIN::TerrainSurface surface);
    void NotifyRenderStateDirty();
    void MarkGeometryChanged() noexcept;
    void CaptureChunkBeforeEdit(const TERRAIN::TerrainChunkKey& key);
    void MarkChunkChanged(const TERRAIN::TerrainChunkKey& key) noexcept;
    void RecordChunkChange(const TERRAIN::TerrainChunkKey& key) noexcept;
    void RecordFullChunkChange() noexcept;
    void InvalidateCoverageCache() noexcept;
    bool TryReadCoverageVertex(int32_t globalX, int32_t globalZ, float& outRawCoverage) const noexcept;
    void RebuildCoverageCache() const;
    void MarkCoverageVertexDirty(const TERRAIN::TerrainChunkKey& chunkKey, uint32_t localX, uint32_t localZ) noexcept;
    void MarkCoverageChunkDirty(const TERRAIN::TerrainChunkKey& chunkKey) noexcept;
    bool SampleLocalRaw(float localX, float localZ, float& outHeight, float& outCoverage) const noexcept;
    bool StitchChunkBorders(const std::vector<TERRAIN::TerrainChunkKey>& touchedChunks,
                            std::set<TERRAIN::TerrainChunkKey>& changedChunks, bool* outHeightChanged = nullptr,
                            bool* outLayersChanged = nullptr);
    TERRAIN::TerrainChunk& EnsureChunk(const TERRAIN::TerrainChunkKey& key, int32_t seedLayer = -1);
    bool SeedChunkFromNeighborEdges(const TERRAIN::TerrainChunkKey& key, TERRAIN::TerrainChunk& chunk);
    // fn が受け取る chunkOrigin は地形オブジェクトローカル空間。
    template <typename Fn>
    void ForEachChunkInBrushLocal(const MATH::Vec3& localPos, float radius, bool createMissingChunks, Fn&& fn,
                                  int32_t seedLayer = -1);

    TERRAIN::TerrainSurface surface_{};
    uint64_t revision_ = 0u;
    uint64_t geometryRevision_ = 0u;
    uint64_t coverageRevision_ = 1u;
    uint64_t collisionRevision_ = 0u;
    uint64_t nextChunkRevision_ = 1u;
    std::map<TERRAIN::TerrainChunkKey, uint64_t> chunkRevisions_{};
    TERRAIN::TerrainChunkChangeJournal chunkChangeJournal_{};
    bool geometryEditBatchActive_ = false;
    bool geometryEditBatchDirty_ = false;
    bool geometryEditDeltaReady_ = false;
    std::string geometryEditBeforePresetId_{};
    std::vector<std::string> geometryEditBeforeLayerPresetIds_{};
    std::map<TERRAIN::TerrainChunkKey, std::optional<TERRAIN::TerrainChunk>> geometryEditBeforeChunks_{};
    mutable uint64_t coverageCacheRevision_ = 0u;
    mutable std::unordered_map<uint64_t, float> coverageCache_{};
    mutable std::unordered_map<uint64_t, float> coverageDistanceCache_{};
    mutable bool coverageCacheInitialized_ = false;
    mutable bool coverageCacheFullRebuild_ = true;
    mutable bool coverageDirtyValid_ = false;
    mutable int32_t coverageDirtyMinX_ = 0;
    mutable int32_t coverageDirtyMaxX_ = 0;
    mutable int32_t coverageDirtyMinZ_ = 0;
    mutable int32_t coverageDirtyMaxZ_ = 0;
};

} // namespace HIKARI
