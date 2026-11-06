#pragma once
#include <array>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI {
namespace INPUT {
class InputSnapshot;
}

enum class CameraControlInputContext {
    EditorViewport,
    RuntimeWindow,
};

struct CameraControlCaptureState {
    bool wantMouse = false;
    bool wantKeyboard = false;
    bool acceptWheel = true;
};

using CameraControlCaptureQuery = CameraControlCaptureState (*)();

void SetCameraControlCaptureQuery(CameraControlCaptureQuery query) noexcept;

struct DebugCamera3DSettings {
    float moveSpeed = 4.0f;      // 地面付近の飛行速度 [m/s]。右クリック中のホイールで 1.25 倍ずつ変える
    float fastMultiplier = 3.0f; // Shift の倍率
    float mouseLookSensitivity = 0.003f;
    float wheelMoveStep = 0.8f; // 右クリックしていない時のホイール 1 目盛りの前後移動 [m]
    float pitchLimitRad = 1.45f;
    // 高さによる速度の倍率: |y| がこの高さを超えると比例して速くなる（50m で 1 倍、1500m で 30 倍）。部屋の大きさの
    // 作業では効かず、雲（1.5km）や地形の上空へは数秒で届く。
    float altitudeSpeedHeight = 50.0f;
};

// 飛行の一フレーム分の操作。動作の割り当て（Editor.Camera*）か生のキーとマウスから作る。
struct DebugCameraControls {
    std::array<float, 2> move{};
    float vertical = 0.0f;
    std::array<float, 2> look{};
    float wheel = 0.0f;
    // 視点を回す（look が効く）。ホイールはこの間だけ速度を変え、それ以外は前後に動かす。
    bool looking = false;
    // move と vertical が効く。
    bool moving = false;
    bool fast = false;
    bool reset = false;
};

class DebugCameraController3D {
public:
    // 動作の割り当て（右クリック中だけ飛ぶ Editor.Camera*）から読む。
    static DebugCameraControls ReadActionControls(const INPUT::InputSnapshot& input,
                                                  CameraControlInputContext inputContext);
    // 生のキーとマウスから読む: WASD と QE、Shift で加速。lookWithoutButton なら右クリック無しでマウスが視点を回す。
    static DebugCameraControls ReadDeviceControls(const INPUT::InputSnapshot& input, bool lookWithoutButton);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void Reset(const MATH::Vec3& position, float yawRad, float pitchRad);
    void ResetFromCamera(const Camera3D& camera);
    void Update(float dt, Camera3D& camera, const INPUT::InputSnapshot& input,
                CameraControlInputContext inputContext = CameraControlInputContext::EditorViewport);
    // 編集器の視口では、ImGui がマウスやキーを使っている間の分を捨ててから動かす。
    void Fly(float dt, Camera3D& camera, DebugCameraControls controls, CameraControlInputContext inputContext);

    const MATH::Vec3& GetPosition() const;
    float GetYaw() const;
    float GetPitch() const;

    void SetPosition(const MATH::Vec3& position);
    void SetYaw(float yawRad);
    void SetPitch(float pitchRad);

    DebugCamera3DSettings& Settings();
    const DebugCamera3DSettings& Settings() const;

private:
    void ApplyToCamera(Camera3D& camera) const;
    float ClampPitch(float pitch) const;

    bool enabled_ = true;
    MATH::Vec3 position_{0.0f, 2.0f, -6.0f};
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    MATH::Vec3 resetPosition_{0.0f, 2.0f, -6.0f};
    float resetYaw_ = 0.0f;
    float resetPitch_ = 0.0f;
    DebugCamera3DSettings settings_{};
};

} // namespace HIKARI
