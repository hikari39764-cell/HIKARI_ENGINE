#pragma once

#include <cstdint>
#include <string>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    using KinematicMotionSourceId = uint64_t;

    inline constexpr KinematicMotionSourceId
        kCharacterLocomotionMotionSource = 1u;

    struct KinematicControllerSettings {
        float maximumSlopeAngleRadians = 0.87266463f;
        float stepUpHeight = 0.4f;
        float stickToFloorDistance = 0.5f;
        float stepForwardTestDistance = 0.15f;
        float characterPadding = 0.02f;
        float predictiveContactDistance = 0.1f;
        float penetrationRecoverySpeed = 1.0f;
        uint32_t maximumCollisionHits = 256u;
        bool enhancedInternalEdgeRemoval = true;
    };

    struct KinematicMotionRequest {
        MATH::Vec3 horizontalVelocity{};
        MATH::Quat desiredRotation = MATH::Quat::Identity();
        float jumpSpeed = 0.0f;
        float maximumFallSpeed = 55.0f;
        bool hasDesiredRotation = false;
        // Physics owns the immediate contact test. The controller may grant
        // a short contact-independent window such as coyote time.
        bool jumpRequested = false;
        bool allowJumpWithoutGroundContact = false;
        KinematicControllerSettings controller{};
    };

    struct KinematicTeleportRequest {
        PhysicsPose pose{};
        bool clearVelocity = true;
    };

    struct KinematicMotionState {
        PhysicsPose pose{};
        MATH::Vec3 velocity{};
        MATH::Vec3 groundVelocity{};
        MATH::Vec3 groundPosition{};
        MATH::Vec3 groundNormal{ 0.0f, 1.0f, 0.0f };
        RuntimeObjectHandle groundObject{};
        PhysicsCharacterGroundState groundState =
            PhysicsCharacterGroundState::InAir;
        bool hitWall = false;
        bool hitCeiling = false;
        bool maximumHitsExceeded = false;

        bool IsGrounded() const noexcept {
            return groundState == PhysicsCharacterGroundState::OnGround;
        }
    };

    enum class KinematicMotionRuntimeHealth : uint8_t {
        Pending,
        Ready,
        RetainedPrevious,
        Error,
    };

    struct KinematicMotionRuntimeStatus {
        KinematicMotionRuntimeHealth health =
            KinematicMotionRuntimeHealth::Pending;
        std::string message{};
    };

    struct KinematicBodyLandedEvent {
        RuntimeObjectHandle object{};
        RuntimeObjectHandle groundObject{};
        MATH::Vec3 position{};
        MATH::Vec3 groundNormal{ 0.0f, 1.0f, 0.0f };
        float impactSpeed = 0.0f;
    };

    struct KinematicBodyLeftGroundEvent {
        RuntimeObjectHandle object{};
        RuntimeObjectHandle previousGroundObject{};
    };

} // namespace HIKARI::PHYSICS
