#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    class TerrainSurfaceComponent;
    class World;
}

namespace HIKARI::PHYSICS {

    inline constexpr uint64_t kTerrainCollisionShapeSourceId =
        0x5445525241494Eull;

    // Terrain authored data から作る immutable な静的衝突 projection。
    // Sand runtime deform はこの revision/geometry に一切入れない。
    struct TerrainCollisionProjection {
        RuntimeObjectHandle object{};
        uint64_t authoredRevision = 0u;
        uint64_t contentRevision = 0u;
        uint32_t triangleCount = 0u;
        std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
        // Empty terrain is a successful projection with geometry == nullptr.
        // Invalid authored input is a failed projection and must not replace
        // the cache's last-known-good entry.
        bool buildSucceeded = false;
        std::string diagnostic{};
    };

    TerrainCollisionProjection BuildTerrainCollisionProjection(
        RuntimeObjectHandle object,
        const TerrainSurfaceComponent& terrain);

    class TerrainCollisionProjectionCache {
    public:
        void Refresh(World& world);
        const TerrainCollisionProjection* Find(
            RuntimeObjectHandle object) const noexcept;
        void Clear() noexcept;

    private:
        std::unordered_map<uint64_t, TerrainCollisionProjection> entries_{};
        std::unordered_map<uint64_t, uint64_t> rejectedRevisions_{};
    };

} // namespace HIKARI::PHYSICS
