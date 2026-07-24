#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::PHYSICS {

    enum class PhysicsMotionType : uint8_t {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class PhysicsShapeType : uint8_t {
        Box,
        Sphere,
        Capsule,
        ConvexHull,
        TriangleMesh,
    };

    enum class PhysicsContactPhase : uint8_t {
        Begin,
        Persist,
        End,
    };

    enum class PhysicsErrorCode : uint8_t {
        None,
        WorldUnavailable,
        TransformInvalid,
        NoEnabledShapes,
        CollisionAssetMissing,
        CollisionAssetInvalid,
        UnsupportedScale,
        UnsupportedMotionShape,
        FullyLockedDynamicBody,
        InvalidBodyDefinition,
        BackendShapeCreationFailed,
        BackendCapacityExceeded,
        BackendStepFailed,
        BackendFailure,
    };

    enum class PhysicsBodyRuntimeState : uint8_t {
        Ready,
        RetainedPrevious,
        PendingRetry,
        Invalid,
    };

    struct PhysicsBodyHandle {
        static constexpr uint32_t InvalidSlot =
            (std::numeric_limits<uint32_t>::max)();

        uint32_t slot = InvalidSlot;
        uint32_t generation = 0;

        bool IsValid() const noexcept {
            return slot != InvalidSlot && generation != 0;
        }

        uint64_t ToValue() const noexcept {
            return (static_cast<uint64_t>(generation) << 32u) |
                static_cast<uint64_t>(slot);
        }

        friend bool operator==(
            const PhysicsBodyHandle&,
            const PhysicsBodyHandle&) = default;
    };

    struct PhysicsCharacterHandle {
        static constexpr uint32_t InvalidSlot =
            (std::numeric_limits<uint32_t>::max)();

        uint32_t slot = InvalidSlot;
        uint32_t generation = 0;

        bool IsValid() const noexcept {
            return slot != InvalidSlot && generation != 0;
        }

        uint64_t ToValue() const noexcept {
            return (static_cast<uint64_t>(generation) << 32u) |
                static_cast<uint64_t>(slot);
        }

        friend bool operator==(
            const PhysicsCharacterHandle&,
            const PhysicsCharacterHandle&) = default;
    };

    struct PhysicsPose {
        MATH::Vec3 position{};
        MATH::Quat rotation = MATH::Quat::Identity();
    };

    struct PhysicsMaterialDesc {
        float friction = 0.5f;
        float restitution = 0.0f;
        float density = 1.0f;
    };

    struct PhysicsCollisionFilter {
        uint32_t layer = 1u;
        uint32_t mask = 0xFFFFFFFFu;
    };

    struct PhysicsGeometryBuffer {
        std::vector<MATH::Vec3> vertices{};
        std::vector<uint32_t> indices{};
    };

    // Stable within the authored collision definition. Asset shapes retain
    // their persistent authoring ID; manual shapes use their component order.
    struct PhysicsShapeKey {
        uint64_t sourceShapeId = 0u;
        uint32_t componentOrdinal = 0u;

        bool IsValid() const noexcept {
            return sourceShapeId != 0u || componentOrdinal != 0u;
        }

        friend bool operator==(
            const PhysicsShapeKey&,
            const PhysicsShapeKey&) = default;
    };

    struct PhysicsShapeDesc {
        PhysicsShapeKey key{};
        PhysicsShapeType type = PhysicsShapeType::Box;
        MATH::Vec3 localCenter{};
        MATH::Quat localRotation = MATH::Quat::Identity();
        MATH::Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        float height = 1.0f;
        MATH::Vec3 localScale{ 1.0f, 1.0f, 1.0f };
        std::shared_ptr<const PhysicsGeometryBuffer> geometry{};
        uint32_t vertexOffset = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexOffset = 0u;
        uint32_t indexCount = 0u;
        uint64_t geometryContentRevision = 0u;
        bool isTrigger = false;
        PhysicsMaterialDesc material{};
        PhysicsCollisionFilter filter{};
    };

    struct PhysicsBodyDesc {
        RuntimeObjectHandle object{};
        PhysicsMotionType motionType = PhysicsMotionType::Static;
        float mass = 1.0f;
        float gravityScale = 1.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        bool allowSleeping = true;
        bool continuousCollision = false;
        bool lockTranslationX = false;
        bool lockTranslationY = false;
        bool lockTranslationZ = false;
        bool lockRotationX = false;
        bool lockRotationY = false;
        bool lockRotationZ = false;
    };

    struct PhysicsBodyCreateInfo {
        PhysicsBodyDesc body{};
        PhysicsPose initialPose{};
        MATH::Vec3 initialLinearVelocity{};
        MATH::Vec3 initialAngularVelocity{};
        std::vector<PhysicsShapeDesc> shapes{};
    };

    struct PhysicsBodyState {
        PhysicsPose pose{};
        MATH::Vec3 linearVelocity{};
        MATH::Vec3 angularVelocity{};
        bool awake = true;
    };

    struct PhysicsBodyCreateResult {
        PhysicsBodyHandle handle{};
        PhysicsMotionType effectiveMotionType =
            PhysicsMotionType::Static;
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message{};
        bool recoverable = false;

        bool Succeeded() const noexcept {
            return handle.IsValid() && error == PhysicsErrorCode::None;
        }
    };

    enum class PhysicsCharacterGroundState : uint8_t {
        OnGround,
        OnSteepGround,
        NotSupported,
        InAir,
    };

    struct PhysicsCharacterDesc {
        RuntimeObjectHandle object{};
        std::vector<PhysicsShapeDesc> shapes{};
        float mass = 70.0f;
        float maxSlopeAngleRadians = 0.87266463f;
        float characterPadding = 0.02f;
        float predictiveContactDistance = 0.1f;
        float penetrationRecoverySpeed = 1.0f;
        uint32_t maxCollisionHits = 256u;
        bool enhancedInternalEdgeRemoval = true;
        PhysicsCollisionFilter filter{};
    };

    struct PhysicsCharacterCreateInfo {
        PhysicsCharacterDesc character{};
        PhysicsPose initialPose{};
        MATH::Vec3 initialLinearVelocity{};
    };

    struct PhysicsCharacterCreateResult {
        PhysicsCharacterHandle handle{};
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message{};
        bool recoverable = false;

        bool Succeeded() const noexcept {
            return handle.IsValid() && error == PhysicsErrorCode::None;
        }
    };

    struct PhysicsCharacterStepSettings {
        MATH::Vec3 gravity{ 0.0f, -9.81f, 0.0f };
        float stepUpHeight = 0.4f;
        float stickToFloorDistance = 0.5f;
        float stepForwardTestDistance = 0.15f;
    };

    struct PhysicsCharacterState {
        PhysicsPose pose{};
        MATH::Vec3 linearVelocity{};
        MATH::Vec3 groundVelocity{};
        MATH::Vec3 groundPosition{};
        MATH::Vec3 groundNormal{ 0.0f, 1.0f, 0.0f };
        RuntimeObjectHandle groundObject{};
        PhysicsCharacterGroundState groundState =
            PhysicsCharacterGroundState::InAir;
        bool hitWall = false;
        bool hitCeiling = false;
        bool maxHitsExceeded = false;

        bool IsGrounded() const noexcept {
            return groundState == PhysicsCharacterGroundState::OnGround;
        }
    };

    struct PhysicsStepResult {
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message{};

        bool Succeeded() const noexcept {
            return error == PhysicsErrorCode::None;
        }
    };

    struct PhysicsBackendStatistics {
        uint32_t bodyCount = 0u;
        uint32_t activeBodyCount = 0u;
        uint32_t bodyCapacity = 0u;
        uint32_t bodyPairCapacity = 0u;
        uint32_t contactConstraintCapacity = 0u;
        uint64_t temporaryAllocatorBytes = 0u;
    };

    struct PhysicsBodyRuntimeStatus {
        RuntimeObjectHandle object{};
        PhysicsBodyHandle body{};
        PhysicsMotionType requestedMotionType =
            PhysicsMotionType::Static;
        PhysicsMotionType effectiveMotionType =
            PhysicsMotionType::Static;
        PhysicsBodyRuntimeState state =
            PhysicsBodyRuntimeState::Invalid;
        PhysicsErrorCode error = PhysicsErrorCode::None;
        uint64_t definitionSignature = 0u;
        uint64_t sourceRevision = 0u;
        uint32_t shapeCount = 0u;
        bool awake = false;
        std::string message{};
    };

    struct PhysicsQueryFilter {
        uint32_t layerMask = 0xFFFFFFFFu;
        bool includeTriggers = true;
        RuntimeObjectHandle ignoredObject{};
    };

    struct PhysicsRaycastQuery {
        MATH::Vec3 origin{};
        MATH::Vec3 direction{ 0.0f, 0.0f, 1.0f };
        float maxDistance = 1000.0f;
        PhysicsQueryFilter filter{};
    };

    struct PhysicsShapeCastQuery {
        PhysicsShapeDesc shape{};
        PhysicsPose startPose{};
        MATH::Vec3 direction{ 0.0f, 0.0f, 1.0f };
        float maxDistance = 1000.0f;
        PhysicsQueryFilter filter{};
    };

    struct PhysicsOverlapQuery {
        PhysicsShapeDesc shape{};
        PhysicsPose pose{};
        PhysicsQueryFilter filter{};
    };

    struct PhysicsHit {
        PhysicsBodyHandle body{};
        RuntimeObjectHandle object{};
        uint32_t shapeIndex = 0;
        PhysicsShapeKey shapeKey{};
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        float distance = 0.0f;
        float fraction = 0.0f;
        bool isTrigger = false;
    };

    struct PhysicsContactEvent {
        PhysicsContactPhase phase = PhysicsContactPhase::Begin;
        PhysicsBodyHandle bodyA{};
        PhysicsBodyHandle bodyB{};
        RuntimeObjectHandle objectA{};
        RuntimeObjectHandle objectB{};
        uint32_t shapeIndexA = 0;
        uint32_t shapeIndexB = 0;
        PhysicsShapeKey shapeKeyA{};
        PhysicsShapeKey shapeKeyB{};
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        float penetrationDepth = 0.0f;
        float impulse = 0.0f;
        bool isTrigger = false;
    };

    struct PhysicsWorldSettings {
        MATH::Vec3 gravity{ 0.0f, -9.81f, 0.0f };
        bool allowSleeping = true;
        bool emitPersistContactEvents = false;
    };

    struct PhysicsBackendCapabilities {
        bool raycast = false;
        bool shapeCast = false;
        bool overlap = false;
        bool continuousCollision = false;
        bool triggerEvents = false;
        bool virtualCharacters = false;
    };

} // namespace HIKARI::PHYSICS
