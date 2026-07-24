#pragma once

#include <cstdint>
#include <vector>

#include "Scene/Camera/HIKARI_CameraRigTypes.h"

namespace HIKARI {

    class Camera3D;

    namespace CAMERA {

        class CameraRigService {
        public:
            void BeginFrame(uint64_t frameIndex) noexcept;
            void Clear() noexcept;

            bool SubmitPose(const CameraRigSubmission& submission);
            bool SubmitModifier(
                const CameraModifierSubmission& submission);

            bool TryResolvePose(
                SceneObjectId cameraObjectId,
                CameraRigSubmission& outSubmission) const noexcept;
            bool TryResolveFallbackCamera(
                SceneObjectId& outCameraObjectId) const noexcept;
            void ApplyModifiers(
                SceneObjectId cameraObjectId,
                bool duringOverride,
                Camera3D& camera) const;

            CameraRigRuntimeStatus GetStatus(
                SceneObjectId cameraObjectId) const noexcept;
            uint64_t GetFrameIndex() const noexcept { return frameIndex_; }

        private:
            static bool IsValidPose(const CameraRigPose& pose) noexcept;
            static bool IsValidModifier(
                const CameraModifierSubmission& modifier) noexcept;

            uint64_t frameIndex_ = 0u;
            std::vector<CameraRigSubmission> poses_{};
            std::vector<CameraModifierSubmission> modifiers_{};
        };

    } // namespace CAMERA

} // namespace HIKARI
