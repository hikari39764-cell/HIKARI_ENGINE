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

    // Pointer coordinates are expressed in the active presentation's logical
    // client pixels. normalizedX/Y are clamped to [0, 1]. Embedded editor
    // views that do not own a standalone presentation surface report valid=0
    // so GameProjects can keep keyboard/gamepad navigation authoritative.
    struct GameProjectPointerState {
        float x = 0.0f;
        float y = 0.0f;
        float normalizedX = 0.0f;
        float normalizedY = 0.0f;
        uint8_t primaryDown = 0u;
        uint8_t secondaryDown = 0u;
        uint8_t valid = 0u;
        uint8_t reserved = 0u;
        uint32_t logicalWidth = 0u;
        uint32_t logicalHeight = 0u;
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
        // Final render-presentation world space.  Gameplay and physics
        // queries continue to use World, which remains authoritative.
        PresentationWorld = 2,
    };

    enum class GameProjectAnimationLayerRootAlignment : uint8_t {
        // Preserve the sampled node-local root transform. This is the
        // compatible choice for overlays authored against the same parent.
        ParentLocal = 0,
        // Preserve the sampled root's model-space rotation while leaving
        // translation and scale under the advancing base pose. Use this for
        // a full-body clip layered over a different locomotion pelvis.
        PreserveModelRotation = 1,
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

    // Optional, per-fixed-tick collision policy for one kinematic move. A
    // valid ignored object is excluded only while the submitted move is
    // solved; every other body and the following tick retain normal
    // collision. Reserved fields must remain zero.
    struct GameProjectKinematicCollisionOptions {
        uint64_t ignoredObjectHandle = 0u;
        uint32_t reserved0 = 0u;
        uint32_t reserved1 = 0u;
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

    // A one-shot authoritative teleport consumed by the kinematic physics
    // owner. Save/load and checkpoint restoration use this instead of writing
    // ObjectView transforms behind the solver.
    struct GameProjectKinematicTeleport {
        uint64_t objectHandle = 0u;
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        GameProjectFloat3 position{};
        GameProjectQuat rotation{};
        uint8_t clearVelocity = 1u;
        uint8_t reserved[3] = { 0u, 0u, 0u };
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

    enum class GameProjectPhysicsContactPhase : uint8_t {
        Begin = 0u,
        Persist = 1u,
        End = 2u,
    };

    // Frame-local fixed-event copy. Runtime handles are valid only in the
    // current World and must never be serialized into a save payload.
    struct GameProjectPhysicsTriggerEvent {
        uint64_t objectA = 0u;
        uint64_t objectB = 0u;
        uint64_t bodyA = 0u;
        uint64_t bodyB = 0u;
        uint64_t sourceShapeIdA = 0u;
        uint64_t sourceShapeIdB = 0u;
        GameProjectFloat3 position{};
        GameProjectFloat3 normal{};
        float penetrationDepth = 0.0f;
        float impulse = 0.0f;
        uint32_t shapeIndexA = 0u;
        uint32_t shapeIndexB = 0u;
        uint32_t componentOrdinalA = 0u;
        uint32_t componentOrdinalB = 0u;
        GameProjectPhysicsContactPhase phase =
            GameProjectPhysicsContactPhase::Begin;
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

    // Last resolved gameplay camera pose. It is a value snapshot only and
    // does not expose Camera3D or CameraDirector ownership across the SDK.
    struct GameProjectCameraPose {
        GameProjectFloat3 eye{};
        GameProjectFloat3 target{ 0.0f, 0.0f, 1.0f };
        GameProjectFloat3 up{ 0.0f, 1.0f, 0.0f };
        float verticalFovRadians = 0.0f;
        float aspectRatio = 1.0f;
        uint32_t reserved = 0u;
    };

    // Secondary-subject composition for the built-in orbit-follow camera.
    // The engine keeps ownership of the primary follow pivot, smoothing and
    // collision. A zero camera handle applies to every active follow camera.
    struct GameProjectCameraTargetFramingRequest {
        uint64_t cameraObjectHandle = 0u;
        uint64_t targetObjectHandle = 0u;
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        GameProjectFloat3 targetPivotOffset{ 0.0f, 1.0f, 0.0f };
        float targetWeight = 0.45f;
        float yawTrackingSpeedDegreesPerSecond = 540.0f;
        float preferredPitchDegrees = 17.0f;
        float pitchTrackingSpeedDegreesPerSecond = 240.0f;
        float distanceScale = 0.18f;
        float maximumDistanceOffset = 2.0f;
        float weight = 1.0f;
        uint8_t suppressOrbitInput = 1u;
        uint8_t suppressZoomInput = 1u;
        uint8_t reserved[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    };

    // Optional response profile for secondary-subject framing. Keeping this
    // separate preserves the SDK 1.8 request layout and lets old callers use
    // the engine-owned default profile.
    struct GameProjectCameraTargetFramingDynamics {
        float enterSmoothSeconds = 0.24f;
        float exitSmoothSeconds = 0.20f;
        float targetSwitchSmoothSeconds = 0.18f;
        float yawSmoothSeconds = 0.14f;
        float pitchSmoothSeconds = 0.20f;
        float focusSmoothSeconds = 0.10f;
        float distanceExpandSmoothSeconds = 0.12f;
        float distanceContractSmoothSeconds = 0.26f;
        float yawDeadZoneDegrees = 1.5f;
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    struct GameProjectTimeDilationRequest {
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        float scale = 1.0f;
        float durationUnscaledSeconds = 0.0f;
        uint32_t reserved = 0u;
    };

    // Engine-owned combat presentation presets. These identify stable
    // presentation intents rather than renderer assets, so GameProjects do
    // not receive VFX programs, texture handles, or particle services.
    enum class GameProjectCombatEffectType : uint8_t {
        LightImpact = 0,
        HeavyImpact = 1,
        SkillImpact = 2,
        PerfectGuardBurst = 3,
        DodgeAfterimage = 4,
    };

    struct GameProjectCombatWorldEffectRequest {
        uint64_t ownerObjectHandle = 0u;
        uint64_t sourceId = 0u;
        GameProjectTransform transform{};
        GameProjectFloat3 direction{ 0.0f, 1.0f, 0.0f };
        float scale = 1.0f;
        float intensity = 1.0f;
        uint32_t randomSeed = 0u;
        GameProjectCombatEffectType effect =
            GameProjectCombatEffectType::LightImpact;
        uint8_t reserved[3] = { 0u, 0u, 0u };
    };

    // One evaluated blade sample. The engine owns history, discontinuity
    // rejection, lifetime and the transparent render pass. Callers submit
    // world-space base/tip positions only while a trail should be visible.
    struct GameProjectWeaponTrailSample {
        uint64_t ownerObjectHandle = 0u;
        uint64_t sourceId = 0u;
        GameProjectFloat3 bladeBaseWorld{};
        GameProjectFloat3 bladeTipWorld{};
        GameProjectFloat3 bodyColor{ 0.08f, 0.52f, 0.92f };
        GameProjectFloat3 edgeColor{ 0.72f, 0.96f, 1.0f };
        float lifetimeUnscaledSeconds = 0.14f;
        float opacity = 0.72f;
        float emissiveIntensity = 1.35f;
        float flowStrength = 0.65f;
        uint32_t reserved = 0u;
    };

    // A transient HDR scene-color reaction. The world center is projected by
    // the render view; UI is composed later and is never distorted. Sequence
    // lets the engine reject duplicate perfect-guard delivery in one source.
    struct GameProjectPerfectGuardWaveRequest {
        uint64_t sourceId = 0u;
        uint64_t sequence = 0u;
        GameProjectFloat3 worldCenter{};
        GameProjectFloat3 color{ 0.34f, 0.82f, 1.0f };
        float durationUnscaledSeconds = 0.52f;
        float maximumScreenRadius = 0.46f;
        float ringThickness = 0.055f;
        float distortionStrength = 0.020f;
        float chromaticAberration = 0.0035f;
        float flashIntensity = 0.42f;
        float intensity = 1.0f;
        uint32_t reserved = 0u;
    };

    // Frame-local full-screen action grade. The caller submits this every
    // presentation frame while the effect is visible; the engine never
    // retains a GameProject pointer or changes gameplay time from this view.
    struct GameProjectActionCinematicGradeRequest {
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        float weight = 0.0f;
        float monochromeStrength = 0.0f;
        float chromaticSeparationPixels = 0.0f;
        float contrastStrength = 0.0f;
        uint32_t reserved = 0u;
    };

    // Frame-local Burst presentation. Unlike a cinematic grade this keeps
    // gameplay color readable and concentrates a hard, animated energy
    // treatment around the screen perimeter. Submit every visible frame.
    struct GameProjectActionBoostGradeRequest {
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        float weight = 0.0f;
        float edgeIntensity = 0.0f;
        float edgeWidth = 0.20f;
        float chromaticSeparationPixels = 0.0f;
        uint32_t reserved = 0u;
    };

} // namespace HIKARI
