#include "HIKARI_DebugCameraController3D.h"
#include <algorithm>
#include <cmath>
#include "Input/Runtime/HIKARI_InputActionIds.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/HIKARI_EditorViewportInput.h"
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        MATH::Vec3 ComputeForward(float yaw, float pitch) {
            const float cp = std::cos(pitch);
            const float sp = std::sin(pitch);
            const float cy = std::cos(yaw);
            const float sy = std::sin(yaw);
            return MATH::Normalize({ sy * cp, sp, cy * cp });
        }
    }

    void DebugCameraController3D::SetEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool DebugCameraController3D::IsEnabled() const {
        return enabled_;
    }

    void DebugCameraController3D::Reset(const MATH::Vec3& position, float yawRad, float pitchRad) {
        resetPosition_ = position;
        resetYaw_ = yawRad;
        resetPitch_ = ClampPitch(pitchRad);

        position_ = resetPosition_;
        yaw_ = resetYaw_;
        pitch_ = resetPitch_;
    }

    void DebugCameraController3D::ResetFromCamera(const Camera3D& camera) {
        const MATH::Vec3 direction = camera.GetTarget() - camera.GetPosition();
        const float length = MATH::Length(direction);
        if (length <= 1e-5f) {
            Reset(camera.GetPosition(), yaw_, pitch_);
            return;
        }

        const MATH::Vec3 forward = direction * (1.0f / length);
        const float yaw = std::atan2(forward.x, forward.z);
        const float pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
        Reset(camera.GetPosition(), yaw, pitch);
    }

    void DebugCameraController3D::Update(
        float dt,
        Camera3D& camera,
        const INPUT::InputSnapshot& input,
        CameraControlInputContext inputContext) {
#if !defined(HIKARI_WITH_EDITOR)
        (void)inputContext;
#endif
        if (!enabled_) {
            ApplyToCamera(camera);
            return;
        }

        bool wantMouse = false;
        bool wantKeyboard = false;
        bool acceptWheel = true;
#if defined(HIKARI_WITH_EDITOR)
        if (inputContext == CameraControlInputContext::EditorViewport &&
            ImGui::GetCurrentContext() != nullptr) {
            ImGuiIO& io = ImGui::GetIO();
            wantMouse = io.WantCaptureMouse;
            wantKeyboard = io.WantCaptureKeyboard;
            acceptWheel = !wantMouse;

            if (EDITOR::HasGameViewportInputRect()) {
                const bool viewportMouseActive = EDITOR::IsGameViewportMouseInputActive();
                const bool viewportKeyboardActive = EDITOR::IsGameViewportKeyboardInputActive();
                const bool viewportWheelActive = EDITOR::IsGameViewportWheelInputActive();

                wantMouse = !viewportMouseActive;
                wantKeyboard = !viewportKeyboardActive;
                acceptWheel = viewportWheelActive;
            }
        }
#endif

        // エディタ操作キー(W/E/R/Q)と衝突しないよう、カメラリセットは Home に寄せる。
        if (!wantKeyboard && input.IsDown(
                INPUT::ActionIds::EditorCameraReset)) {
            position_ = resetPosition_;
            yaw_ = resetYaw_;
            pitch_ = resetPitch_;
        }

        const bool rightMouseDown = input.IsDown(
            INPUT::ActionIds::EditorCameraLookHeld);
        if (!wantMouse && rightMouseDown) {
            const std::array<float, 2> look = input.GetAxis2D(
                INPUT::ActionIds::EditorCameraLook);
            yaw_ += look[0] * settings_.mouseLookSensitivity;
            pitch_ -= look[1] * settings_.mouseLookSensitivity;
            pitch_ = ClampPitch(pitch_);
        }

        const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
        const MATH::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
        const MATH::Vec3 right = MATH::Normalize(MATH::Cross(worldUp, forward));

        // WASD/QE は右クリック中だけ飛行カメラとして扱う。
        const bool movementInputActive =
            inputContext == CameraControlInputContext::RuntimeWindow ||
            rightMouseDown;
        if (!wantKeyboard && movementInputActive) {
            const std::array<float, 2> moveAxes = input.GetAxis2D(
                INPUT::ActionIds::EditorCameraMove);
            const float vertical = input.GetAxis1D(
                INPUT::ActionIds::EditorCameraMoveVertical);
            MATH::Vec3 move =
                right * moveAxes[0] +
                forward * moveAxes[1] +
                worldUp * vertical;

            const bool fast = input.IsDown(
                INPUT::ActionIds::EditorCameraBoost);
            float speed = settings_.moveSpeed;
            if (fast) {
                speed *= settings_.fastMultiplier;
            }

            const float moveLen = MATH::Length(move);
            if (moveLen > 1e-5f) {
                position_ = position_ + MATH::Normalize(move) * (speed * dt);
            }
        }

        if (acceptWheel) {
            const float wheel = input.GetAxis1D(
                INPUT::ActionIds::EditorCameraZoom);
            if (std::abs(wheel) > 1e-6f) {
                position_ = position_ + forward * (wheel * settings_.wheelMoveStep);
            }
        }

        ApplyToCamera(camera);
    }

    const MATH::Vec3& DebugCameraController3D::GetPosition() const {
        return position_;
    }

    float DebugCameraController3D::GetYaw() const {
        return yaw_;
    }

    float DebugCameraController3D::GetPitch() const {
        return pitch_;
    }

    void DebugCameraController3D::SetPosition(const MATH::Vec3& position) {
        position_ = position;
    }

    void DebugCameraController3D::SetYaw(float yawRad) {
        yaw_ = yawRad;
    }

    void DebugCameraController3D::SetPitch(float pitchRad) {
        pitch_ = ClampPitch(pitchRad);
    }

    DebugCamera3DSettings& DebugCameraController3D::Settings() {
        return settings_;
    }

    const DebugCamera3DSettings& DebugCameraController3D::Settings() const {
        return settings_;
    }

    void DebugCameraController3D::ApplyToCamera(Camera3D& camera) const {
        const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
        camera.SetLookAt(position_, position_ + forward);
    }

    float DebugCameraController3D::ClampPitch(float pitch) const {
        return std::clamp(pitch, -settings_.pitchLimitRad, settings_.pitchLimitRad);
    }

} // namespace HIKARI
