#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

    class World;
    class GameObject;
    struct RuntimePlayStateService;

    namespace SAND {
        class SandSurfaceQueryService;
        struct SandChunkRuntimeState;
        using SandChunkRuntimeStatePtr =
            std::shared_ptr<SandChunkRuntimeState>;
        using SandChunkRuntimeSnapshotPtr =
            std::shared_ptr<const SandChunkRuntimeState>;
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
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void Update(
            World& world,
            const FrameContext& frame) override;
        void PreRender(
            World& world,
            const FrameContext& frame) override;
        bool RaycastBathymetry(
            const WATER::WaterBathymetryRay& ray,
            float& outDistance) const noexcept override;

    private:
        struct ObjectChunks {
            // chunkKey -> 可変状態。shared_ptr で安定させる。
            std::unordered_map<uint64_t, SAND::SandChunkRuntimeStatePtr>
                chunks{};
            std::unordered_map<uint64_t, SAND::SandChunkRuntimeSnapshotPtr>
                snapshots{};
            std::unordered_map<uint64_t, uint64_t>
                snapshotRevisions{};
            std::unordered_map<uint64_t, uint64_t>
                authoredHeightSignatures{};
            std::unordered_map<uint64_t, uint64_t>
                authoredCoverageSignatures{};
            uint64_t lastRevision = 0u;
            uint64_t lastCoverageRevision = 0u;
            float lastCoverageEdgeFalloff = 0.0f;
            bool hasCoverageProjectionSettings = false;
            // 高さ場はオブジェクトローカル空間。スタンプ源のワールド中心を
            // ここに掛けてローカルへそろえる（Refresh で 1 度だけ計算）。
            MATH::Mat4 localToWorld{};
            MATH::Mat4 worldToLocal{};
            MATH::Mat4 publishedLocalToWorld{};
            bool hasPublishedTransform = false;
            GameObject* owner = nullptr;
            float scaleX = 1.0f;
            float scaleY = 1.0f;
            float scaleZ = 1.0f;
        };

        void Refresh(World& world);
        // 唯一の TerrainInteractionSample stream を chunk へ適用する。
        // runtime 高さが実際に変わった場合だけ true。PostFixed で直ちに
        // immutable snapshot を再発行し、PreRender の consumer に渡す。
        bool ApplyInteractions(
            std::span<const TERRAIN::TerrainInteractionSample> requests,
            float deltaSeconds);
        bool RelaxChunk(
            SAND::SandChunkRuntimeState& chunk,
            float fixedDt);
        bool StitchRuntimeBorders(ObjectChunks& bucket);
        void RebuildCombinedHeights(SAND::SandChunkRuntimeState& chunk);
        bool ApplyStylizedImprint(
            SAND::SandChunkRuntimeState& chunk,
            TERRAIN::TerrainFootprintKind footprintKind,
            float localX,
            float localZ,
            float localRadiusX,
            float localRadiusZ,
            float localVerticalRadius,
            const MATH::Vec2& localTangent,
            const MATH::Vec2& localBitangent,
            float edgeSoftness,
            float sinkDepth,
            bool holdOnly);
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
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        // objectKey (render stable id) -> chunk 状態表
        std::unordered_map<uint64_t, ObjectChunks> objects_{};
        bool hasPublishedSnapshot_ = false;
    };

} // namespace HIKARI
