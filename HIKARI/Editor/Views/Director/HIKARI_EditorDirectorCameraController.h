#pragma once

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::EDITOR {

struct DirectorCameraPose {
  MATH::Vec3 position{};
  MATH::Quat rotation = MATH::Quat::Identity();
};

struct EditorDirectorCameraInput {
  bool lookActive = false;
  bool orbitActive = false;
  bool panActive = false;
  bool moveForward = false;
  bool moveBackward = false;
  bool moveRight = false;
  bool moveLeft = false;
  bool moveUp = false;
  bool moveDown = false;
  bool fast = false;
  float mouseDeltaX = 0.0f;
  float mouseDeltaY = 0.0f;
  float wheelDelta = 0.0f;
};

struct EditorDirectorCameraSettings {
  float moveSpeed = 5.0f;
  float fastMultiplier = 4.0f;
  float lookSensitivity = 0.003f;
  float orbitSensitivity = 0.004f;
  float panSensitivity = 0.0025f;
  float wheelMoveStep = 1.0f;
  float pitchLimitRad = 1.52f;
  float minimumOrbitDistance = 0.05f;
};

struct EditorDirectorCameraState {
  MATH::Vec3 position{};
  MATH::Vec3 orbitPivot{};
  float orbitDistance = 5.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float fovYRad = 1.0471975512f;
  float aspect = 16.0f / 9.0f;
  float nearZ = 0.1f;
  float farZ = 100.0f;
  EditorDirectorCameraSettings settings{};
};

enum class EditorDirectorCameraViewPreset : uint8_t {
  Perspective,
  Top,
  Front,
  Right,
};

class EditorDirectorCameraController {
public:
  EditorDirectorCameraController();

  void Reset(const MATH::Vec3 &position = {0.0f, 2.0f, -6.0f},
             float yawRad = 0.0f, float pitchRad = 0.0f);
  void ResetFromCamera(const Camera3D &camera);
  void SetPose(const DirectorCameraPose &pose);
  DirectorCameraPose GetPose() const noexcept;
  EditorDirectorCameraState CaptureState() const noexcept;
  bool RestoreState(const EditorDirectorCameraState &state) noexcept;

  bool Update(const EditorDirectorCameraInput &input, float deltaTime,
              float aspect);
  void Focus(const MATH::Vec3 &worldPosition, float preferredDistance = 5.0f);
  void ApplyViewPreset(EditorDirectorCameraViewPreset preset);

  const Camera3D &GetCamera() const noexcept;
  Camera3D &GetCamera() noexcept;
  const MATH::Vec3 &GetOrbitPivot() const noexcept;

  EditorDirectorCameraSettings &Settings() noexcept;
  const EditorDirectorCameraSettings &Settings() const noexcept;

private:
  void ApplyToCamera(float aspect);
  MATH::Vec3 ComputeForward() const;
  float ClampPitch(float pitch) const;

  Camera3D camera_{};
  MATH::Vec3 position_{0.0f, 2.0f, -6.0f};
  MATH::Vec3 orbitPivot_{0.0f, 2.0f, -1.0f};
  float orbitDistance_ = 5.0f;
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  EditorDirectorCameraSettings settings_{};
};

} // namespace HIKARI::EDITOR
