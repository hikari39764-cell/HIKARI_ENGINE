#pragma once

namespace HIKARI {

    class Camera3D;

    struct RuntimePlayStateService {
        const bool* active = nullptr;

        bool IsActive() const noexcept {
            return active != nullptr && *active;
        }
    };

    struct GameplayCameraService {
        Camera3D* camera = nullptr;
        const bool* runtimeSceneCameraActive = nullptr;

        bool IsRuntimeCameraActive() const noexcept {
            return runtimeSceneCameraActive != nullptr &&
                *runtimeSceneCameraActive;
        }
    };

} // namespace HIKARI
