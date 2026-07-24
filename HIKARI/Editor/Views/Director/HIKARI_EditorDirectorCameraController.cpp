#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

namespace {
constexpr float kMinimumAspect = 0.05f;
constexpr float kMinimumDeltaTime = 0.0f;
constexpr float kMaximumDeltaTime = 0.1f;

MATH::Vec3 RotationForward(const MATH::Quat &rotation) {
  const MATH::Mat4 matrix = MATH::Mat4::Rotate(rotation);
  const MATH::Vec4 direction = matrix.TransformPoint({0.0f, 0.0f, 1.0f, 0.0f});
  const MATH::Vec3 forward{direction.x, direction.y, direction.z};
  return MATH::Length(forward) > 1.0e-5f ? MATH::Normalize(forward)
                                         : MATH::Vec3{0.0f, 0.0f, 1.0f};
}
} // namespace

EditorDirectorCameraController::EditorDirectorCameraController() { Reset(); }

void EditorDirectorCameraController::Reset(const MATH::Vec3 &position,
                                           float yawRad, float pitchRad) {

  position_ = position;
  yaw_ = yawRad;
  pitch_ = ClampPitch(pitchRad);
  orbitDistance_ = 5.0f;
  orbitPivot_ = position_ + ComputeForward() * orbitDistance_;
  ApplyToCamera(camera_.GetAspect());
}

void EditorDirectorCameraController::ResetFromCamera(const Camera3D &camera) {

  position_ = camera.GetPosition();
  MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
  const float length = MATH::Length(forward);
  if (length <= 1.0e-5f) {
    forward = {0.0f, 0.0f, 1.0f};
    orbitDistance_ = 5.0f;
  } else {
    forward = forward * (1.0f / length);
    orbitDistance_ = (std::max)(length, settings_.minimumOrbitDistance);
  }
  yaw_ = std::atan2(forward.x, forward.z);
  pitch_ = ClampPitch(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
  orbitPivot_ = position_ + forward * orbitDistance_;
  camera_ = camera;
  ApplyToCamera(camera.GetAspect());
}

void EditorDirectorCameraController::SetPose(const DirectorCameraPose &pose) {

  position_ = pose.position;
  const MATH::Vec3 forward = RotationForward(pose.rotation);
  yaw_ = std::atan2(forward.x, forward.z);
  pitch_ = ClampPitch(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
  orbitDistance_ = (std::max)(orbitDistance_, settings_.minimumOrbitDistance);
  orbitPivot_ = position_ + forward * orbitDistance_;
  ApplyToCamera(camera_.GetAspect());
}

DirectorCameraPose EditorDirectorCameraController::GetPose() const noexcept {
  DirectorCameraPose pose{};
  pose.position = position_;
  pose.rotation = MATH::Quat::FromEulerXYZ(-pitch_, yaw_, 0.0f);
  return pose;
}

bool EditorDirectorCameraController::Update(
    const EditorDirectorCameraInput &input, float deltaTime, float aspect) {

  const MATH::Vec3 previousPosition = position_;
  const float previousYaw = yaw_;
  const float previousPitch = pitch_;
  const float previousOrbitDistance = orbitDistance_;
  const MATH::Vec3 previousPivot = orbitPivot_;

  const MATH::Vec3 worldUp{0.0f, 1.0f, 0.0f};
  if (input.lookActive) {
    yaw_ += input.mouseDeltaX * settings_.lookSensitivity;
    pitch_ = ClampPitch(pitch_ - input.mouseDeltaY * settings_.lookSensitivity);
    orbitPivot_ = position_ + ComputeForward() * orbitDistance_;
  }

  if (input.orbitActive) {
    yaw_ += input.mouseDeltaX * settings_.orbitSensitivity;
    pitch_ =
        ClampPitch(pitch_ - input.mouseDeltaY * settings_.orbitSensitivity);
    position_ = orbitPivot_ - ComputeForward() * orbitDistance_;
  }

  MATH::Vec3 forward = ComputeForward();
  MATH::Vec3 right = MATH::Cross(worldUp, forward);
  if (MATH::Length(right) <= 1.0e-5f) {
    right = {1.0f, 0.0f, 0.0f};
  } else {
    right = MATH::Normalize(right);
  }
  const MATH::Vec3 up = MATH::Normalize(MATH::Cross(forward, right));

  if (input.panActive) {
    const float panScale =
        (std::max)(orbitDistance_, 1.0f) * settings_.panSensitivity;
    const MATH::Vec3 pan = right * (-input.mouseDeltaX * panScale) +
                           up * (input.mouseDeltaY * panScale);
    position_ = position_ + pan;
    orbitPivot_ = orbitPivot_ + pan;
  }

  const float safeDeltaTime =
      std::clamp(deltaTime, kMinimumDeltaTime, kMaximumDeltaTime);
  MATH::Vec3 move{};
  if (input.moveForward)
    move = move + forward;
  if (input.moveBackward)
    move = move - forward;
  if (input.moveRight)
    move = move + right;
  if (input.moveLeft)
    move = move - right;
  if (input.moveUp)
    move = move + worldUp;
  if (input.moveDown)
    move = move - worldUp;
  if (MATH::Length(move) > 1.0e-5f) {
    float speed = settings_.moveSpeed;
    if (input.fast) {
      speed *= settings_.fastMultiplier;
    }
    const MATH::Vec3 offset = MATH::Normalize(move) * (speed * safeDeltaTime);
    position_ = position_ + offset;
    orbitPivot_ = orbitPivot_ + offset;
  }

  if (std::abs(input.wheelDelta) > 1.0e-6f) {
    const MATH::Vec3 offset =
        forward * (input.wheelDelta * settings_.wheelMoveStep);
    position_ = position_ + offset;
    if (input.orbitActive) {
      orbitDistance_ = (std::max)(settings_.minimumOrbitDistance,
                                  MATH::Length(orbitPivot_ - position_));
    } else {
      orbitPivot_ = orbitPivot_ + offset;
    }
  }

  ApplyToCamera(aspect);
  return MATH::Length(position_ - previousPosition) > 1.0e-6f ||
         MATH::Length(orbitPivot_ - previousPivot) > 1.0e-6f ||
         std::abs(yaw_ - previousYaw) > 1.0e-6f ||
         std::abs(pitch_ - previousPitch) > 1.0e-6f ||
         std::abs(orbitDistance_ - previousOrbitDistance) > 1.0e-6f;
}

void EditorDirectorCameraController::Focus(const MATH::Vec3 &worldPosition,
                                           float preferredDistance) {

  orbitDistance_ =
      (std::max)(preferredDistance, settings_.minimumOrbitDistance);
  orbitPivot_ = worldPosition;
  position_ = orbitPivot_ - ComputeForward() * orbitDistance_;
  ApplyToCamera(camera_.GetAspect());
}

void EditorDirectorCameraController::ApplyViewPreset(
    EditorDirectorCameraViewPreset preset) {

  constexpr float kQuarterTurn = 1.57079632679f;
  constexpr float kPerspectiveYaw = -0.78539816339f;
  constexpr float kPerspectivePitch = -0.43633231299f;
  switch (preset) {
  case EditorDirectorCameraViewPreset::Top:
    yaw_ = 0.0f;
    pitch_ = -settings_.pitchLimitRad;
    break;
  case EditorDirectorCameraViewPreset::Front:
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    break;
  case EditorDirectorCameraViewPreset::Right:
    yaw_ = -kQuarterTurn;
    pitch_ = 0.0f;
    break;
  case EditorDirectorCameraViewPreset::Perspective:
  default:
    yaw_ = kPerspectiveYaw;
    pitch_ = kPerspectivePitch;
    break;
  }

  orbitDistance_ = (std::max)(orbitDistance_, settings_.minimumOrbitDistance);
  position_ = orbitPivot_ - ComputeForward() * orbitDistance_;
  ApplyToCamera(camera_.GetAspect());
}

const Camera3D &EditorDirectorCameraController::GetCamera() const noexcept {
  return camera_;
}

Camera3D &EditorDirectorCameraController::GetCamera() noexcept {
  return camera_;
}

const MATH::Vec3 &
EditorDirectorCameraController::GetOrbitPivot() const noexcept {
  return orbitPivot_;
}

EditorDirectorCameraSettings &
EditorDirectorCameraController::Settings() noexcept {
  return settings_;
}

const EditorDirectorCameraSettings &
EditorDirectorCameraController::Settings() const noexcept {
  return settings_;
}

void EditorDirectorCameraController::ApplyToCamera(float aspect) {
  camera_.SetPerspective(camera_.GetFovYRad(),
                         (std::max)(aspect, kMinimumAspect), camera_.GetNearZ(),
                         camera_.GetFarZ());
  const MATH::Vec3 forward = ComputeForward();
  camera_.SetLookAt(position_, position_ + forward);
}

MATH::Vec3 EditorDirectorCameraController::ComputeForward() const {
  const float cosPitch = std::cos(pitch_);
  return MATH::Normalize(
      {std::sin(yaw_) * cosPitch, std::sin(pitch_), std::cos(yaw_) * cosPitch});
}

float EditorDirectorCameraController::ClampPitch(float pitch) const {
  return std::clamp(pitch, -settings_.pitchLimitRad, settings_.pitchLimitRad);
}

} // namespace HIKARI::EDITOR
