#pragma once

#include <cstdint>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI {
    class ColliderComponent;
    class GameObject;
}

namespace HIKARI::PHYSICS {

    class PhysicsCollisionGeometryStore;
    struct TerrainCollisionProjection;

    struct PhysicsBodyBuildResult {
        bool hasDefinition = false;
        bool success = false;
        PhysicsErrorCode error = PhysicsErrorCode::None;
        uint64_t sourceRevision = 0u;
        std::string message{};
    };

    PhysicsShapeDesc BuildPhysicsShapeDesc(
        const ColliderComponent& collider,
        const MATH::Vec3& worldScale,
        uint32_t componentOrdinal = 0u);

    PhysicsBodyBuildResult BuildPhysicsBodyCreateInfo(
        const GameObject& object,
        PhysicsBodyCreateInfo& outCreateInfo,
        MATH::Vec3& outWorldScale,
        PhysicsCollisionGeometryStore* collisionGeometryStore = nullptr,
        const TerrainCollisionProjection* terrainProjection = nullptr,
        const MATH::Vec3* retainedDefinitionScale = nullptr);

    uint64_t ComputePhysicsBodyDefinitionSignature(
        const PhysicsBodyCreateInfo& createInfo,
        const MATH::Vec3& worldScale) noexcept;

    // 完全な definition は数百の immutable terrain chunk shape を含み得る。
    // allocation-free な source signature で body definition の全入力を先に
    // 検証し、同一なら巨大な descriptor payload の構築自体を省略する。
    uint64_t ComputePhysicsBodyDefinitionSourceSignature(
        const GameObject& object,
        uint64_t collisionGeometrySourceRevision,
        const TerrainCollisionProjection* terrainProjection = nullptr,
        const MATH::Vec3* retainedDefinitionScale = nullptr) noexcept;

    bool TryGetPhysicsWorldPoseAndScale(
        const GameObject& object,
        PhysicsPose& outPose,
        MATH::Vec3& outScale);

    bool ArePhysicsPosesNearlyEqual(
        const PhysicsPose& left,
        const PhysicsPose& right) noexcept;

    bool ApplyPhysicsWorldPose(
        GameObject& object,
        const PhysicsPose& pose);

} // namespace HIKARI::PHYSICS
