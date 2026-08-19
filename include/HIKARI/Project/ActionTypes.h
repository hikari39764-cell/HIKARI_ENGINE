#pragma once

#include <cstdint>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    struct GameProjectFloat2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct GameProjectQuat {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct GameProjectTransform {
        GameProjectFloat3 position{};
        GameProjectQuat rotation{};
        GameProjectFloat3 scale{ 1.0f, 1.0f, 1.0f };
    };

    enum class GameProjectInputDevice : uint8_t {
        None = 0,
        KeyboardMouse = 1,
        Gamepad = 2,
    };

    enum class GameProjectInputValueType : uint8_t {
        Button = 0,
        Axis1D = 1,
        Axis2D = 2,
    };

    struct GameProjectInputActionState {
        float x = 0.0f;
        float y = 0.0f;
        float heldSeconds = 0.0f;
        GameProjectInputValueType valueType =
            GameProjectInputValueType::Button;
        uint8_t down = 0u;
        uint8_t pressed = 0u;
        uint8_t released = 0u;
    };

    struct GameProjectAnimationState {
        uint64_t clipId = 0u;
        uint64_t poseRevision = 0u;
        float timeSeconds = 0.0f;
        float durationSeconds = 0.0f;
        float normalizedTime = 0.0f;
        float speed = 1.0f;
        float transitionWeight = 1.0f;
        uint8_t playing = 0u;
        uint8_t finished = 0u;
        uint8_t transitioning = 0u;
        uint8_t loop = 0u;
        uint8_t valid = 0u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    enum class GameProjectAnimationSocketSpace : uint8_t {
        Local = 0,
        World = 1,
    };

    // Translation and rotation are expressed in character/model axes. The
    // action owner chooses facing policy and converts translation to world
    // space before SubmitMotionDelta; the SDK never teleports the object or
    // silently assumes a locomotion orientation.
    struct GameProjectRootMotionDelta {
        GameProjectFloat3 translation{};
        GameProjectQuat rotation{};
        uint64_t fixedTickIndex = 0u;
        uint64_t revision = 0u;
        uint8_t valid = 0u;
        uint8_t reserved[7] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    };

    struct GameProjectKinematicMove {
        uint64_t objectHandle = 0u;
        uint64_t sourceId = 0u;
        uint64_t fixedTickIndex = 0u;
        int32_t priority = 0;
        GameProjectFloat3 horizontalVelocity{};
        GameProjectQuat desiredRotation{};
        float jumpSpeed = 0.0f;
        float maximumFallSpeed = 55.0f;
        uint8_t hasDesiredRotation = 0u;
        uint8_t jumpRequested = 0u;
        uint8_t allowJumpWithoutGroundContact = 0u;
        uint8_t reserved = 0u;
    };

    // A collision-aware relative contribution consumed by the kinematic
    // character solver for one fixed tick. Translation is in world space;
    // rotation is a relative quaternion composed after locomotion facing.
    struct GameProjectKinematicMotionDelta {
        uint64_t objectHandle = 0u;
        uint64_t sourceId = 0u;
        uint64_t fixedTickIndex = 0u;
        int32_t priority = 0;
        GameProjectFloat3 translation{};
        GameProjectQuat rotation{};
        uint32_t reserved = 0u;
    };

    enum class GameProjectGroundState : uint8_t {
        OnGround = 0,
        OnSteepGround = 1,
        NotSupported = 2,
        InAir = 3,
    };

    struct GameProjectKinematicState {
        GameProjectTransform pose{};
        GameProjectFloat3 velocity{};
        GameProjectFloat3 groundVelocity{};
        GameProjectFloat3 groundPosition{};
        GameProjectFloat3 groundNormal{ 0.0f, 1.0f, 0.0f };
        uint64_t groundObjectHandle = 0u;
        GameProjectGroundState groundState =
            GameProjectGroundState::InAir;
        uint8_t grounded = 0u;
        uint8_t hitWall = 0u;
        uint8_t hitCeiling = 0u;
        uint8_t maximumHitsExceeded = 0u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    struct GameProjectPhysicsQueryFilter {
        uint64_t ignoredObjectHandle = 0u;
        uint32_t layerMask = 0xFFFFFFFFu;
        uint8_t includeTriggers = 1u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    enum class GameProjectPhysicsShapeType : uint8_t {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
    };

    struct GameProjectPhysicsShape {
        GameProjectFloat3 center{};
        GameProjectQuat rotation{};
        GameProjectFloat3 halfExtents{ 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        float height = 1.0f;
        GameProjectPhysicsShapeType type =
            GameProjectPhysicsShapeType::Sphere;
        uint8_t isTrigger = 0u;
        uint8_t reserved[2] = { 0u, 0u };
    };

    struct GameProjectPhysicsRaycastQuery {
        GameProjectFloat3 origin{};
        GameProjectFloat3 direction{ 0.0f, 0.0f, 1.0f };
        float maxDistance = 1000.0f;
        GameProjectPhysicsQueryFilter filter{};
    };

    struct GameProjectPhysicsShapeCastQuery {
        GameProjectPhysicsShape shape{};
        GameProjectFloat3 startPosition{};
        GameProjectQuat startRotation{};
        GameProjectFloat3 direction{ 0.0f, 0.0f, 1.0f };
        float maxDistance = 1000.0f;
        GameProjectPhysicsQueryFilter filter{};
    };

    struct GameProjectPhysicsOverlapQuery {
        GameProjectPhysicsShape shape{};
        GameProjectFloat3 position{};
        GameProjectQuat rotation{};
        GameProjectPhysicsQueryFilter filter{};
    };

    struct GameProjectPhysicsHit {
        uint64_t objectHandle = 0u;
        uint64_t bodyHandle = 0u;
        uint64_t sourceShapeId = 0u;
        GameProjectFloat3 position{};
        GameProjectFloat3 normal{};
        float distance = 0.0f;
        float fraction = 0.0f;
        uint32_t shapeIndex = 0u;
        uint32_t componentOrdinal = 0u;
        uint8_t isTrigger = 0u;
        uint8_t reserved[7] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    };

    struct GameProjectCameraModifier {
        uint64_t cameraObjectHandle = 0u;
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        GameProjectFloat3 worldPositionOffset{};
        GameProjectFloat3 localPositionOffset{};
        GameProjectFloat3 worldTargetOffset{};
        GameProjectFloat3 localTargetOffset{};
        float verticalFovOffsetRadians = 0.0f;
        float weight = 1.0f;
        uint8_t applyDuringOverride = 0u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    // Kept separate from GameProjectCameraModifier so SDK 1.5 modules retain
    // the exact value layout they were compiled against.
    struct GameProjectCameraRollModifier {
        uint64_t cameraObjectHandle = 0u;
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        float localRollRadians = 0.0f;
        float weight = 1.0f;
        uint8_t applyDuringOverride = 0u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    struct GameProjectTimeDilationRequest {
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        float scale = 1.0f;
        float durationUnscaledSeconds = 0.0f;
        uint32_t reserved = 0u;
    };

} // namespace HIKARI
