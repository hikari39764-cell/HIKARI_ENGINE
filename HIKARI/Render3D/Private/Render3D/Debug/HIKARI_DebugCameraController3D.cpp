#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include <algorithm>
#include <cmath>
#include "Input/Runtime/HIKARI_InputActionIds.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

namespace {
CameraControlCaptureQuery gCaptureQuery = nullptr;

MATH::Vec3 ComputeForward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    return MATH::Normalize({sy * cp, sp, cy * cp});
}
} // namespace

void SetCameraControlCaptureQuery(CameraControlCaptureQuery query) noexcept { gCaptureQuery = query; }

void DebugCameraController3D::SetEnabled(bool enabled) { enabled_ = enabled; }

bool DebugCameraController3D::IsEnabled() const { return enabled_; }

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

DebugCameraControls DebugCameraController3D::ReadActionControls(const INPUT::InputSnapshot& input,
                                                                CameraControlInputContext inputContext) {
    DebugCameraControls controls{};
    controls.looking = input.IsDown(INPUT::ActionIds::EditorCameraLookHeld);
    controls.look = input.GetAxis2D(INPUT::ActionIds::EditorCameraLook);
    controls.wheel = input.GetAxis1D(INPUT::ActionIds::EditorCameraZoom);
    // WASD/QE は右クリック中だけ飛行カメラとして扱う。
    controls.moving = inputContext == CameraControlInputContext::RuntimeWindow || controls.looking;
    controls.move = input.GetAxis2D(INPUT::ActionIds::EditorCameraMove);
    controls.vertical = input.GetAxis1D(INPUT::ActionIds::EditorCameraMoveVertical);
    controls.fast = input.IsDown(INPUT::ActionIds::EditorCameraBoost);
    // エディタ操作キー(W/E/R/Q)と衝突しないよう、カメラリセットは Home に寄せる。
    controls.reset = input.IsDown(INPUT::ActionIds::EditorCameraReset);
    return controls;
}

DebugCameraControls DebugCameraController3D::ReadDeviceControls(const INPUT::InputSnapshot& input,
                                                                bool lookWithoutButton) {
    // 仮想キーコード（Win32 の VK_*）。文字キーは大文字の ASCII と同じ。
    constexpr size_t kShift = 0x10, kHome = 0x24;
    const INPUT::InputDeviceState& devices = input.GetDevices();
    const auto key = [&devices](size_t code) { return devices.keyboard[code] != 0u ? 1.0f : 0.0f; };
    DebugCameraControls controls{};
    controls.looking = lookWithoutButton || devices.mouseButtons[1] != 0u;
    controls.look = {devices.mouseDeltaX, devices.mouseDeltaY};
    controls.wheel = devices.mouseWheel;
    controls.moving = true;
    controls.move = {key('D') - key('A'), key('W') - key('S')};
    controls.vertical = key('E') - key('Q');
    controls.fast = key(kShift) != 0.0f;
    controls.reset = key(kHome) != 0.0f;
    return controls;
}

void DebugCameraController3D::Update(float dt, Camera3D& camera, const INPUT::InputSnapshot& input,
                                     CameraControlInputContext inputContext) {
    Fly(dt, camera, ReadActionControls(input, inputContext), inputContext);
}

void DebugCameraController3D::Fly(float dt, Camera3D& camera, DebugCameraControls controls,
                                  CameraControlInputContext inputContext) {
    if (!enabled_) {
        ApplyToCamera(camera);
        return;
    }

    bool wantMouse = false;
    if (inputContext == CameraControlInputContext::EditorViewport && gCaptureQuery != nullptr) {
        const CameraControlCaptureState capture = gCaptureQuery();
        wantMouse = capture.wantMouse;
        if (capture.wantKeyboard) {
            controls.moving = false;
            controls.reset = false;
        }
        if (!capture.acceptWheel) {
            controls.wheel = 0.0f;
        }
    }

    if (controls.reset) {
        position_ = resetPosition_;
        yaw_ = resetYaw_;
        pitch_ = resetPitch_;
    }

    if (!wantMouse && controls.looking) {
        yaw_ -= controls.look[0] * settings_.mouseLookSensitivity;
        pitch_ -= controls.look[1] * settings_.mouseLookSensitivity;
        pitch_ = ClampPitch(pitch_);
    }

    const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
    const MATH::Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const MATH::Vec3 right = MATH::BuildViewBasisRH(forward, worldUp).right;

    // 視点を回している間のホイールは速度の調整（1 目盛りで 1.25 倍）、それ以外は前後の移動。
    const float wheel = controls.wheel;
    if (std::abs(wheel) > 1e-6f && controls.looking && !wantMouse) {
        settings_.moveSpeed = std::clamp(settings_.moveSpeed * std::pow(1.25f, wheel), 0.1f, 2000.0f);
    }
    // 高さによる倍率（速度と前後の移動の両方）。
    const float altitudeScale =
        (std::max)(1.0f, std::abs(position_.y) / (std::max)(settings_.altitudeSpeedHeight, 1.0f));

    if (controls.moving) {
        MATH::Vec3 move = right * controls.move[0] + forward * controls.move[1] + worldUp * controls.vertical;

        float speed = settings_.moveSpeed * altitudeScale;
        if (controls.fast) {
            speed *= settings_.fastMultiplier;
        }

        const float moveLen = MATH::Length(move);
        if (moveLen > 1e-5f) {
            position_ = position_ + MATH::Normalize(move) * (speed * dt);
        }
    }

    if (std::abs(wheel) > 1e-6f && !controls.looking) {
        position_ = position_ + forward * (wheel * settings_.wheelMoveStep * altitudeScale);
    }

    ApplyToCamera(camera);
}

const MATH::Vec3& DebugCameraController3D::GetPosition() const { return position_; }

float DebugCameraController3D::GetYaw() const { return yaw_; }

float DebugCameraController3D::GetPitch() const { return pitch_; }

void DebugCameraController3D::SetPosition(const MATH::Vec3& position) { position_ = position; }

void DebugCameraController3D::SetYaw(float yawRad) { yaw_ = yawRad; }

void DebugCameraController3D::SetPitch(float pitchRad) { pitch_ = ClampPitch(pitchRad); }

DebugCamera3DSettings& DebugCameraController3D::Settings() { return settings_; }

const DebugCamera3DSettings& DebugCameraController3D::Settings() const { return settings_; }

void DebugCameraController3D::ApplyToCamera(Camera3D& camera) const {
    const MATH::Vec3 forward = ComputeForward(yaw_, pitch_);
    camera.SetLookAt(position_, position_ + forward);
}

float DebugCameraController3D::ClampPitch(float pitch) const {
    return std::clamp(pitch, -settings_.pitchLimitRad, settings_.pitchLimitRad);
}

} // namespace HIKARI
