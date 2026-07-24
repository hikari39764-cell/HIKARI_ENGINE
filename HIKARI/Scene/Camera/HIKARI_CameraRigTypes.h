#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI::CAMERA {

    using CameraRigSourceId = uint64_t;

    constexpr CameraRigSourceId MakeCameraRigSourceId(
        std::string_view stableName) noexcept {

        CameraRigSourceId value = 14695981039346656037ull;
        for (const char character : stableName) {
            value ^= static_cast<unsigned char>(character);
            value *= 1099511628211ull;
        }
        return value != 0u ? value : 1u;
    }

    struct CameraRigPose {
        MATH::Vec3 eye{};
        MATH::Vec3 target{ 0.0f, 0.0f, 1.0f };
        MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
        bool overrideVerticalFov = false;
        float verticalFovRadians = 0.0f;
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
        int priority = 0;
        MATH::Vec3 worldPositionOffset{};
        MATH::Vec3 localPositionOffset{};
        MATH::Vec3 worldTargetOffset{};
        MATH::Vec3 localTargetOffset{};
        float verticalFovOffsetRadians = 0.0f;
        float weight = 1.0f;
        bool applyDuringOverride = false;
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
