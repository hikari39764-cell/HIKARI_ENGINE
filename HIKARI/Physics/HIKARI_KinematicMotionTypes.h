#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    using KinematicMotionSourceId = uint64_t;

    inline constexpr KinematicMotionSourceId
        kCharacterLocomotionMotionSource = 1u;

    struct KinematicControllerSettings {
        float maximumSlopeAngleRadians = 0.73303829f;
        float stepUpHeight = 0.25f;
        float stickToFloorDistance = 0.28f;
        float stepForwardTestDistance = 0.30f;
        float characterPadding = 0.02f;
        float predictiveContactDistance = 0.1f;
        float penetrationRecoverySpeed = 1.0f;
        float edgeProbeDistance = 0.45f;
        float edgeProbeRadius = 0.18f;
        float maximumGroundedDrop = 0.35f;
        uint32_t maximumCollisionHits = 256u;
        bool cancelVelocityTowardsSteepSlopes = true;
        bool edgeGuardEnabled = true;
        bool enhancedInternalEdgeRemoval = true;
    };

    struct KinematicTraversalSupportSample {
        bool hasHit = false;
        float heightDelta = 0.0f;
        float normalUpDot = 0.0f;
        float horizontalDistance = 0.0f;
    };

    // Ground traversal is evaluated after locomotion, root motion and
    // knockback have been combined. This contract keeps small terrain relief
    // traversable while rejecting steep support and unsupported ledges.
    inline bool IsKinematicTraversalSupportWalkable(
        const KinematicControllerSettings& settings,
        const KinematicTraversalSupportSample& sample) noexcept {

        if (!settings.edgeGuardEnabled) {
            return true;
        }
        if (!sample.hasHit ||
            !std::isfinite(sample.heightDelta) ||
            !std::isfinite(sample.normalUpDot) ||
            !std::isfinite(sample.horizontalDistance)) {
            return false;
        }

        const float slopeRadians = std::clamp(
            settings.maximumSlopeAngleRadians,
            0.0f,
            1.55334306f);
        const float horizontalDistance = (std::max)(
            sample.horizontalDistance, 0.0f);
        const float padding = (std::max)(
            settings.characterPadding, 0.0f);
        const float maximumRise = (std::max)(
            (std::max)(settings.stepUpHeight, 0.0f),
            std::tan(slopeRadians) * horizontalDistance) + padding;
        const float maximumDrop = (std::max)(
            settings.maximumGroundedDrop, 0.0f) + padding;
        const float minimumUpDot = std::cos(slopeRadians);
        return sample.normalUpDot >= minimumUpDot &&
            sample.heightDelta <= maximumRise &&
            sample.heightDelta >= -maximumDrop;
    }

    struct KinematicMotionRequest {
        // A transient fixed-tick collision exception. Physics still filters
        // every other body through the authored layer/mask contract.
        RuntimeObjectHandle ignoredCollisionObject{};
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

    // A relative, fixed-tick motion contribution. Translation is expressed in
    // physics/world coordinates by the submitter; rotation is a relative
    // quaternion that the physics integration composes with the current pose.
    struct KinematicMotionDelta {
        MATH::Vec3 translation{};
        MATH::Quat rotation = MATH::Quat::Identity();
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
