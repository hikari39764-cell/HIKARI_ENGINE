#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI::CAMERA {

    using CameraRigSourceId = uint64_t;

    constexpr CameraRigSourceId MakeCameraRigSourceId(
        std::string_view stableName) noexcept {

        CameraRigSourceId value = HASH::Fnv1aBytes(
            HASH::kFnv1a64OffsetBasis,
            stableName.data(),
            stableName.size());
        return value != 0u ? value : 1u;
    }

    struct CameraRigPose {
        MATH::Vec3 eye{};
        MATH::Vec3 target{ 0.0f, 0.0f, 1.0f };
        MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
        bool overrideClipPlanes = false;
        float nearClip = 0.05f, farClip = 2000;
        bool overrideVerticalFov = false;
        float verticalFovRadians = 0.0f;
        // Scales ordinary post-pose modifiers during an engine-owned camera
        // handoff. A continuous value avoids reintroducing a one-frame pop
        // when an exclusive relationship shot enters or releases.
        float modifierWeightScale = 1.0f;
    };

    struct CameraModeSubmission {
        SceneObjectId cameraObjectId;
        CameraRigSourceId sourceId = 0;
        int priority = 0;
        std::string mode;
    };
    struct CameraRigSubmission {
        SceneObjectId cameraObjectId{};
        CameraRigSourceId sourceId = 0u;
        int priority = 0;
        CameraRigPose pose{};
    };

    struct CameraModifierSubmission {
        SceneObjectId cameraObjectId{};
        CameraRigSourceId sourceId = 0u;
        // A source may submit independent presentation channels in the same
        // frame (for example the stable SDK modifier and the SDK 1.6 roll
        // extension). The channel only selects the per-frame storage slot;
        // sourceId continues to own priority and deterministic ordering.
        uint64_t channelId = 0u;
        int priority = 0;
        MATH::Vec3 worldPositionOffset{};
        MATH::Vec3 localPositionOffset{};
        MATH::Vec3 worldTargetOffset{};
        MATH::Vec3 localTargetOffset{};
        float verticalFovOffsetRadians = 0.0f;
        float localRollRadians = 0.0f;
        float weight = 1.0f;
        bool applyDuringOverride = false;
    };

    struct CameraTargetFramingDynamics {
        float enterSmoothSeconds = 0.24f;
        float exitSmoothSeconds = 0.20f;
        float targetSwitchSmoothSeconds = 0.18f;
        float yawSmoothSeconds = 0.14f;
        float pitchSmoothSeconds = 0.20f;
        float focusSmoothSeconds = 0.10f;
        float distanceExpandSmoothSeconds = 0.12f;
        float distanceContractSmoothSeconds = 0.26f;
        float yawDeadZoneDegrees = 1.5f;
    };

    // Per-frame secondary-target framing for an orbit-follow camera.  The
    // follow system keeps ownership of its primary pivot, smoothing and wall
    // collision; gameplay only supplies the extra subject and composition
    // policy.  This prevents lock-on cameras from replacing the canonical
    // camera pose or duplicating collision handling in a game module.
    struct CameraTargetFramingSubmission {
        SceneObjectId cameraObjectId{};
        RuntimeObjectHandle targetObject{};
        CameraRigSourceId sourceId = 0u;
        int priority = 0;
        MATH::Vec3 targetPivotOffset{ 0.0f, 1.0f, 0.0f };
        float targetWeight = 0.45f;
        float yawTrackingSpeedDegreesPerSecond = 540.0f;
        float preferredPitchDegrees = 17.0f;
        float pitchTrackingSpeedDegreesPerSecond = 240.0f;
        float distanceScale = 0.18f;
        float maximumDistanceOffset = 2.0f;
        float weight = 1.0f;
        bool suppressOrbitInput = true;
        bool suppressZoomInput = true;
        CameraTargetFramingDynamics dynamics{};
        bool relationshipShot = false;
        float relationshipPreferredSide = 0.0f;
        float relationshipShoulderAngleDegrees = 0.0f;
        float relationshipVerticalFovOffsetRadians = 0.0f;
        float relationshipRollRadians = 0.0f;
        float relationshipSideClearanceBias = 0.0f;
        float relationshipDistanceOffset = 0.0f;
        float relationshipPivotHeightOffset = 0.0f;
        float relationshipFocusLateralOffset = 0.0f;
        float relationshipFocusHeightOffset = 0.0f;
        bool relationshipSuppressModifiers = false;
    };

    struct CameraRigRuntimeStatus {
        uint64_t frameIndex = 0u;
        SceneObjectId cameraObjectId{};
        CameraRigSourceId sourceId = 0u;
        int priority = 0;
        size_t modifierCount = 0u;
        bool poseDriven = false;
    };

} // namespace HIKARI::CAMERA
