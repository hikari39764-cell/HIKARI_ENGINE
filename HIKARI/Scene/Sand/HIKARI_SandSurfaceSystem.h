#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

    class World;

    class PresentationTransformService;
    namespace SAND {
        class SandSurfaceQueryService;
        struct SandChunkRuntimeState;
        using SandChunkRuntimeStatePtr =
            std::shared_ptr<SandChunkRuntimeState>;
    }

    // 疎 chunk 化された地形の物理・変形システム。
    // 各 chunk が独立した高さ場を持ち、球の接触を stamp として適用し、
    // 回充/拡散で緩和する。描画は snapshot 経由で chunk 高さを読む。
    class SandSurfaceSystem final :
        public ISystem,
        public WATER::IWaterBathymetryProvider {
    public:
        std::string_view GetName() const override {
            return "SandSurfaceSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PreRender(
            World& world,
            const FrameContext& frame) override;
        bool RaycastBathymetry(
            const WATER::WaterBathymetryRay& ray,
            float& outDistance) const noexcept override;

    public:
        // 変形スタンプ源の識別。球・キャラクターの足・車輪など、
        // あらゆる「沈み込む物」を統一的に扱うための抽象。
        // 新しい種類を足すときはここへ追加し、GatherImprintSources で
        // 対応する Component を列挙するだけでよい。
        enum class ImprintSourceKind : uint8_t {
            Sphere = 0,   // 球（Collider sphere + dynamic body）
            Footprint,    // キャラクターの足裏（将来）
            Wheel,        // 車輪（将来）
            Box,          // 直方体（将来）
            Custom,       // 任意形状（将来）
        };

        // 一度の沈み込み要求。ソース非依存で、形状は球近似。
        struct ImprintRequest {
            MATH::Vec3 centerWS{};   // ワールド中心
            float radius = 0.5f;     // 接触半径（ワールド）
            float sinkDepth = 0.1f;  // 沈み深さ
            float strength = 1.0f;   // 凹みの強さ係数
        };

    private:
        struct ObjectChunks {
            // chunkKey -> 可変状態。shared_ptr で安定させる。
            std::unordered_map<uint64_t, SAND::SandChunkRuntimeStatePtr>
                chunks{};
            uint64_t lastRevision = 0u;
        };

        void Refresh(World& world);
        // 変形スタンプ源を集めて chunk へ適用する。
        void ApplyImprintSources(World& world, float fixedDt);
        // 現在アクティブなスタンプ源を列挙する（拡張点）。
        void GatherImprintSources(
            World& world,
            std::vector<ImprintRequest>& outRequests);
        void ApplySphereContactStamps(World& world, float fixedDt);
        void RelaxChunk(
            SAND::SandChunkRuntimeState& chunk,
            float fixedDt);
        void RebuildCombinedHeights(SAND::SandChunkRuntimeState& chunk);
        void ApplySphereImprint(
            SAND::SandChunkRuntimeState& chunk,
            float worldX,
            float worldZ,
            float worldRadius,
            float sinkDepth);
        void SyncChunkFromAuthored(
            SAND::SandChunkRuntimeState& chunk,
            const TERRAIN::TerrainChunk& authored);

        static uint64_t ChunkStateKey(
            const TERRAIN::TerrainChunkKey& key) noexcept {
            return (static_cast<uint64_t>(
                        static_cast<uint32_t>(key.x)) << 32u) |
                static_cast<uint32_t>(key.z);
        }

        SAND::SandSurfaceQueryService* queryService_ = nullptr;
        WATER::WaterSurfaceQueryService* waterQueryService_ = nullptr;
        PresentationTransformService* presentationTransforms_ = nullptr;
        // objectKey (render stable id) -> chunk 状態表
        std::unordered_map<uint64_t, ObjectChunks> objects_{};
        std::unordered_map<uint64_t, MATH::Vec3> previousSphereCenters_{};
    };

} // namespace HIKARI
