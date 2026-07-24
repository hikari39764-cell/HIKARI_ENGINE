#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"
#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI {
    class AssetDatabase;
    class ColliderComponent;
}

namespace HIKARI::PHYSICS {

    // Resolves HIKARI collision artifacts into backend-neutral runtime shapes.
    // Vendor-specific cooked data never crosses this boundary.
    class PhysicsCollisionGeometryStore {
    public:
        struct AppendResult {
            bool success = false;
            PhysicsErrorCode error = PhysicsErrorCode::None;
            uint64_t contentRevision = 0u;
            uint32_t appendedShapeCount = 0u;
            std::string message{};
        };

        void SetAssetDatabase(AssetDatabase* assetDatabase) noexcept;
        void Clear() noexcept;

        AppendResult AppendShapes(
            const ColliderComponent& collider,
            const MATH::Vec3& worldScale,
            uint32_t componentOrdinal,
            std::vector<PhysicsShapeDesc>& outShapes);

        uint64_t GetSourceRevision() const noexcept;

    private:
        struct CacheEntry {
            std::filesystem::path artifactPath{};
            uint64_t databaseRevision = 0u;
            uint64_t contentRevision = 0u;
            std::vector<ASSETS::COLLISION::CollisionGeometryShape>
                shapes{};
            std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
        };

        struct FailureEntry {
            std::filesystem::path artifactPath{};
            uint64_t databaseRevision = 0u;
            PhysicsErrorCode error = PhysicsErrorCode::None;
            std::string message{};
        };

        bool ResolveArtifactPath(
            const std::string& assetId,
            std::filesystem::path& outPath) const;
        const CacheEntry* Load(
            const std::string& assetId,
            PhysicsErrorCode& outError,
            std::string& outMessage);

        AssetDatabase* assetDatabase_ = nullptr;
        std::unordered_map<std::string, CacheEntry> cache_{};
        std::unordered_map<std::string, FailureEntry> failures_{};
    };

} // namespace HIKARI::PHYSICS
