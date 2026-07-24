#pragma once

#include <cstdint>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI {
    class ColliderComponent;
    class GameObject;
}

namespace HIKARI::PHYSICS {

    class PhysicsCollisionGeometryStore;

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
        const MATH::Vec3* retainedDefinitionScale = nullptr);

    uint64_t ComputePhysicsBodyDefinitionSignature(
        const PhysicsBodyCreateInfo& createInfo,
        const MATH::Vec3& worldScale) noexcept;

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
