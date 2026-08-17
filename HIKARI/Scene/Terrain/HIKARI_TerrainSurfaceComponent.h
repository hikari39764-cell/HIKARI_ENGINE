#pragma once

#include <string_view>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI {

    // 統合地形地表コンポーネント。固体地形を「ワールド XZ に整列した疎
    // chunk 表」として保持する。筆刷が届いた場所に chunk が生え、1 つの
    // コンポーネントが任意範囲の地形を担う。水面は独立コンポーネント。
    //
    // 着色は従来の SandSurface パイプラインを chunk ごとに再利用し、
    // 見た目（アニメ調沙地）を変えない。物理・変形も chunk 単位で駆動する。
    class TerrainSurfaceComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "TerrainSurfaceComponent";
        }

        const TERRAIN::TerrainSurface& GetSurface() const noexcept;
        void SetSurface(const TERRAIN::TerrainSurface& surface);

        void ApplySolidPreset(std::string_view presetId);

        // ── ワールド空間ブラシ ────────────────────────────
        // 以降の編集 API はすべてワールド座標を取る。内部で chunk を
        // 定位・生成し、該当 chunk のローカル高さ場へスタンプする。
        TERRAIN::TerrainBrush GetBrush() const noexcept;
        void SetBrushMode(TERRAIN::TerrainBrushMode mode);
        void SetBrushRadius(float radius);

        // 起伏スカルプト。worldPos を中心に brush を 1 回適用する。
        // deltaSeconds: フレーム時間。strength は「毎秒の伸び」として
        // 扱い、dt を掛けてフレームレート非依存にする（成熟ツール相当）。
        void SculptAtWorld(
            const MATH::Vec3& worldPos,
            float deltaSeconds = 1.0f / 60.0f);
        // 層ペイント。presetId の層を worldPos 周辺へ塗る。
        void PaintLayerAtWorld(
            std::string_view presetId,
            const MATH::Vec3& worldPos);
        // coverage（実体/穴）を worldPos 周辺へ塗る。target=1 で実体。
        void PaintCoverageAtWorld(
            float target,
            const MATH::Vec3& worldPos);

        void ResetTerrain();

        // ── 物理 / 変形 ───────────────────────────────────
        // ワールド XZ の地形高さをサンプルする。chunk が無い場所は false。
        bool SampleWorldHeight(
            const MATH::Vec3& worldPos,
            float& outHeight,
            MATH::Vec3& outNormal) const noexcept;

        uint32_t GetLayerCount() const noexcept;
        uint64_t GetRevision() const noexcept;

        // chunk 走査用。描画・衝突・変形システムがここから chunk を得る。
        const TERRAIN::TerrainChunkMap& GetChunks() const noexcept;
        float GetChunkWorldSize() const noexcept;
        uint32_t GetChunkCells() const noexcept;

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        void NotifyRenderStateDirty();
        TERRAIN::TerrainChunk& EnsureChunk(
            const TERRAIN::TerrainChunkKey& key);
        // worldPos 周辺のブラシ半径が触れる全 chunk を走査して fn を呼ぶ。
        // fn は (chunk, その chunk 原点のワールド XZ) を受け取る。
        template <typename Fn>
        void ForEachChunkInBrush(const MATH::Vec3& worldPos, Fn&& fn);

        TERRAIN::TerrainSurface surface_{};
        uint64_t revision_ = 0u;
    };

} // namespace HIKARI
