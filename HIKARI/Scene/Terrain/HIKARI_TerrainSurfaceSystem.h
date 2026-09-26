#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

class World;
class GameObject;
struct RuntimePlayStateService;
namespace TERRAIN {
struct TerrainSurface;
}

namespace TERRAIN {
class TerrainSurfaceQueryService;
struct TerrainChunkRuntimeState;
using TerrainChunkRuntimeStatePtr = std::shared_ptr<TerrainChunkRuntimeState>;
using TerrainChunkRuntimeSnapshotPtr = std::shared_ptr<const TerrainChunkRuntimeState>;
} // namespace TERRAIN

// 疎 chunk 化された地形の物理・変形システム。
// 各 chunk が独立した高さ場を持ち、球の接触を stamp として適用し、
// 回充/拡散で緩和する。描画は snapshot 経由で chunk 高さを読む。
class TerrainSurfaceSystem final : public ISystem, public WATER::IWaterBathymetryProvider {
public:
    std::string_view GetName() const override { return "TerrainSurfaceSystem"; }

    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    void PreFixedUpdate(World& world, const FrameContext& frame) override;
    void PostFixedUpdate(World& world, const FrameContext& frame) override;
    void Update(World& world, const FrameContext& frame) override;
    void PreRender(World& world, const FrameContext& frame) override;
    bool RaycastBathymetry(const WATER::WaterBathymetryRay& ray, float& outDistance) const noexcept override;

private:
    struct ObjectChunks {
        // chunkKey -> 可変状態。shared_ptr で安定させる。
        std::unordered_map<uint64_t, TERRAIN::TerrainChunkRuntimeStatePtr> chunks{};
        std::unordered_map<uint64_t, TERRAIN::TerrainChunkRuntimeSnapshotPtr> snapshots{};
        std::unordered_map<uint64_t, uint64_t> snapshotRevisions{};
        std::unordered_map<uint64_t, uint64_t> authoredHeightSignatures{};
        // layer weights は terrain revision で hash し、deform policy は
        // registry 変更を拾うため Refresh ごとに比較する。
        std::unordered_map<uint64_t, uint64_t> authoredLayerPolicySignatures{};
        std::unordered_map<uint64_t, uint64_t> authoredCoverageSignatures{};
        // simulation / snapshot は authored chunk 総数ではなく、実際に
        // 形状が変化した sparse set だけを fixed tick で処理する。
        std::unordered_set<uint64_t> activeDeformationKeys{};
        std::unordered_set<uint64_t> dirtySnapshotKeys{};
        uint64_t lastRevision = 0u;
        uint64_t lastCoverageRevision = 0u;
        uint64_t lastLayerRegistryRevision = 0u;
        float lastCoverageEdgeFalloff = 0.0f;
        bool hasCoverageProjectionSettings = false;
        TERRAIN::TerrainSurfaceSettings lastSettings{};
        bool hasSettings = false;
        float chunkWorldSize = 0.0f;
        float maximumStampRadius = 0.0f;
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
    bool ApplyInteractions(std::span<const TERRAIN::TerrainInteractionSample> requests, float deltaSeconds);
    bool StitchRuntimeBorders(ObjectChunks& bucket, std::span<const uint64_t> dirtyKeys);

    static uint64_t ChunkStateKey(const TERRAIN::TerrainChunkKey& key) noexcept {
        return (static_cast<uint64_t>(static_cast<uint32_t>(key.x)) << 32u) | static_cast<uint32_t>(key.z);
    }

    TERRAIN::TerrainSurfaceQueryService* queryService_ = nullptr;
    WATER::WaterSurfaceQueryService* waterQueryService_ = nullptr;
    const RuntimePlayStateService* runtimePlayState_ = nullptr;
    // objectKey (render stable id) -> chunk 状態表
    std::unordered_map<uint64_t, ObjectChunks> objects_{};
    bool hasPublishedSnapshot_ = false;
    bool runtimeSnapshotDirty_ = false;
};

} // namespace HIKARI
