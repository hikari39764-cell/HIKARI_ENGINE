#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Terrain/HIKARI_TerrainChunks.h"

namespace HIKARI {
    class TerrainSurfaceComponent;
    class World;
}
namespace HIKARI::PHYSICS {

    inline constexpr uint64_t kTerrainCollisionShapeSourceId =
        0x5445525241494Eull;

    struct TerrainCollisionChunkShape {
        TERRAIN::TerrainChunkKey key{};
        uint64_t sourceRevision = 0u;
        uint64_t contentRevision = 0u;
        uint32_t triangleCount = 0u;
        std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
    };

    // Terrain authored data から作る immutable な静的衝突 projection。
    // Sand runtime deform はこの revision/geometry に一切入れない。
    struct TerrainCollisionProjection {
        RuntimeObjectHandle object{};
        // TerrainSurface 全体の authoring revision で cache を無効化する。
        // geometryRevision は既存 diagnostic が使う
        // authored height / coverage revision として保持する。
        uint64_t sourceRevision = 0u;
        uint64_t authoredRevision = 0u;
        uint64_t contentRevision = 0u;
        uint32_t triangleCount = 0u;
        std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
        // Runtime physics は owner 全体を一枚の巨大 mesh に戻さず、chunk
        // shape 群を Jolt StaticCompound へ渡す。geometry は direct build / nav
        // 互換用で、cache-published projection では chunkShapes が主経路。
        std::vector<TerrainCollisionChunkShape> chunkShapes{};
        // Empty terrain は geometry == nullptr の successful projection とする。
        // 不正な authored input は failed projection とし、cache の
        // last-known-good entry を置き換えない。
        bool buildSucceeded = false;
        std::string diagnostic{};
    };

    TerrainCollisionProjection BuildTerrainCollisionProjection(
        RuntimeObjectHandle object,
        const TerrainSurfaceComponent& terrain);
    TerrainCollisionProjection BuildTerrainChunkCollisionProjection(
        RuntimeObjectHandle object,
        const TerrainSurfaceComponent& terrain,
        const TERRAIN::TerrainChunkKey& key);

    class TerrainCollisionProjectionCache {
    public:
        void Refresh(World& world);
        const TerrainCollisionProjection* Find(
            RuntimeObjectHandle object) const noexcept;
        void Clear() noexcept;

    private:
        struct OwnerCacheEntry {
            TerrainCollisionProjection projection{};
            std::map<
                TERRAIN::TerrainChunkKey,
                TerrainCollisionChunkShape> chunks{};
            std::map<TERRAIN::TerrainChunkKey, uint64_t>
                rejectedChunkRevisions{};
            uint64_t sourceRevision = 0u;
            uint64_t chunkChangeRevision = 0u;
        };
        std::unordered_map<uint64_t, OwnerCacheEntry> entries_{};
    };

} // namespace HIKARI::PHYSICS
