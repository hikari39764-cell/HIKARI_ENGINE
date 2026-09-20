#include "HIKARI_ShotPresentation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace HIKARI::SEQUENCER {
namespace {
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
float Safe(float v, float fallback = 0.0f) {
  return std::isfinite(v) ? v : fallback;
}
MATH::Vec3 Mix(MATH::Vec3 a, MATH::Vec3 b, float t) { return a + (b - a) * t; }
float Hermite(float a, float b, float out, float in, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * out +
         (-2 * t3 + 3 * t2) * b + (t3 - t2) * in;
}
MATH::Vec3 Rotate(MATH::Vec3 v, MATH::Vec3 axis, float angle) {
  axis = MATH::Normalize(axis);
  return v * std::cos(angle) + MATH::Cross(axis, v) * std::sin(angle) +
         axis * (MATH::Dot(axis, v) * (1.0f - std::cos(angle)));
}
MATH::Vec3 Point(const MATH::Mat4 &m, MATH::Vec3 p) {
  return {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
          m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
          m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2]};
}
void Normalize(PresentationParameter &p) {
  p.value = Safe(p.value);
  std::erase_if(p.keys, [](const auto &k) {
    return !std::isfinite(k.time) || !std::isfinite(k.value);
  });
  for (auto &k : p.keys) {
    k.time = (std::max)(0.0f, k.time);
    k.inSlope = Safe(k.inSlope);
    k.outSlope = Safe(k.outSlope);
  }
  std::stable_sort(
      p.keys.begin(), p.keys.end(),
      [](const auto &a, const auto &b) { return a.time < b.time; });
}
template <class T> void Ids(std::vector<T> &items) {
  std::unordered_set<uint64_t> ids;
  uint64_t next = 1;
  for (auto &item : items) {
    if (!item.id || !ids.insert(item.id).second) {
      while (ids.contains(next))
        ++next;
      item.id = next;
      ids.insert(next++);
    }
  }
}
} // namespace

float PresentationParameter::Evaluate(float seconds) const noexcept {
  if (keys.empty())
    return value;
  if (seconds < keys.front().time)
    return keys.front().value;
  for (size_t i = 1; i < keys.size(); ++i) {
    if (seconds >= keys[i].time)
      continue;
    const auto &a = keys[i - 1];
    const auto &b = keys[i];
    const float dt = b.time - a.time;
    if (dt <= 0.00001f)
      return b.value;
    return Hermite(a.value, b.value, a.outSlope * dt, b.inSlope * dt,
                   (seconds - a.time) / dt);
  }
  return keys.back().value;
}

void NormalizeShotCamera(ShotCamera &shot) {
  if (shot.poses.empty())
    shot.poses.push_back({});
  for (auto &p : shot.poses) {
    p.time = (std::max)(0.0f, Safe(p.time));
    p.fov = std::clamp(Safe(p.fov, 55), 1.0f, 179.0f);
    p.roll = Safe(p.roll);
    p.incoming = Safe(p.incoming);
    p.outgoing = Safe(p.outgoing);
    p.eye = {Safe(p.eye.x), Safe(p.eye.y, 2), Safe(p.eye.z, -8)};
    p.target = {Safe(p.target.x), Safe(p.target.y, 1), Safe(p.target.z)};
  }
  std::stable_sort(
      shot.poses.begin(), shot.poses.end(),
      [](const auto &a, const auto &b) { return a.time < b.time; });
  shot.targetOffset = {Safe(shot.targetOffset.x), Safe(shot.targetOffset.y),
                       Safe(shot.targetOffset.z)};
  shot.nearClip = (std::max)(0.001f, Safe(shot.nearClip, 0.1f));
  shot.farClip = (std::max)(shot.nearClip + 0.01f, Safe(shot.farClip, 2000));
  Ids(shot.motion);
  for (auto &e : shot.motion) {
    Normalize(e.amount);
    Normalize(e.weight);
    e.frequency = std::clamp(Safe(e.frequency, 3), 0.0f, 100.0f);
  }
}

void NormalizeScreenEffects(std::vector<SequenceScreenEffect> &effects) {
  Ids(effects);
  for (auto &e : effects) {
    e.start = (std::max)(0.0f, Safe(e.start));
    e.duration = (std::max)(0.01f, Safe(e.duration, 3));
    e.fadeIn = std::clamp(Safe(e.fadeIn), 0.0f, e.duration);
    e.fadeOut = std::clamp(Safe(e.fadeOut), 0.0f, e.duration);
    Normalize(e.amount);
    Normalize(e.weight);
    e.color = {Safe(e.color.x, 1), Safe(e.color.y, 1), Safe(e.color.z, 1)};
  }
}

void SampleScreenEffects(const std::vector<SequenceScreenEffect> &effects,
                         float seconds,
                         std::vector<ScreenEffectSample> &output) {
  for (const auto &e : effects) {
    const float local = seconds - e.start;
    if (!e.enabled || local < 0 || local >= e.duration)
      continue;
    float w = std::clamp(e.weight.Evaluate(local), 0.0f, 1.0f);
    if (e.fadeIn > 0)
      w *= std::clamp(local / e.fadeIn, 0.0f, 1.0f);
    if (e.fadeOut > 0)
      w *= std::clamp((e.duration - local) / e.fadeOut, 0.0f, 1.0f);
    if (w > 0)
      output.push_back({e.kind, e.amount.Evaluate(local), w, e.color});
  }
}

ShotPose CaptureShotPose(const Camera3D &camera, float seconds,
                         const MATH::Mat4 *anchor) {
  ShotPose pose;
  pose.time = seconds;
  pose.eye = camera.GetPosition();
  pose.target = camera.GetTarget();
  pose.fov = camera.GetFovYRad() / radians;
  auto cameraUp = camera.GetUp();
  if (anchor) {
    const auto inverse = MATH::Inverse(*anchor);
    pose.eye = Point(inverse, pose.eye);
    pose.target = Point(inverse, pose.target);
    cameraUp = Point(inverse, cameraUp) - Point(inverse, {});
  }
  const auto basis = MATH::BuildViewBasisRH(pose.target - pose.eye, {0, 1, 0});
  pose.roll = std::atan2(MATH::Dot(cameraUp, basis.right),
                         MATH::Dot(cameraUp, basis.up)) /
              radians;
  return pose;
}

Camera3D EvaluateShotCamera(const ShotCamera &shot, float seconds, float aspect,
                            const MATH::Mat4 *anchor,
                            const MATH::Vec3 *lookAt) {
  ShotPose pose = shot.poses.empty() ? ShotPose{} : shot.poses.front();
  for (size_t i = 1; i < shot.poses.size(); ++i) {
    const auto &a = shot.poses[i - 1];
    const auto &b = shot.poses[i];
    if (seconds >= b.time) {
      pose = b;
      continue;
    }
    const float t = std::clamp(
        (seconds - a.time) / (std::max)(0.00001f, b.time - a.time), 0.0f, 1.0f);
    const float s = Hermite(0, 1, a.outgoing, b.incoming, t);
    pose.eye = Mix(a.eye, b.eye, s);
    pose.target = Mix(a.target, b.target, s);
    pose.fov = a.fov + (b.fov - a.fov) * s;
    pose.roll = a.roll + (b.roll - a.roll) * s;
    break;
  }
  auto eye = pose.eye, target = pose.target;
  MATH::Vec3 up{0, 1, 0};
  if (anchor) {
    eye = Point(*anchor, eye);
    target = Point(*anchor, target);
    up = MATH::Normalize(Point(*anchor, up) - Point(*anchor, {}));
  }
  if (lookAt)
    target = *lookAt + shot.targetOffset;
  for (const auto &effect : shot.motion) {
    if (!effect.enabled)
      continue;
    const float amount =
        effect.amount.Evaluate(seconds) *
        std::clamp(effect.weight.Evaluate(seconds), 0.0f, 1.0f);
    const auto basis = MATH::BuildViewBasisRH(target - eye, up);
    auto forward = basis.forward, right = basis.right, vertical = basis.up;
    MATH::Vec3 offset{};
    switch (effect.kind) {
    case CameraMotionKind::Dolly:
      offset = forward * amount;
      break;
    case CameraMotionKind::Truck:
      offset = right * amount;
      break;
    case CameraMotionKind::Pedestal:
      offset = vertical * amount;
      break;
    case CameraMotionKind::Orbit:
      eye = target + Rotate(eye - target, up, amount * radians);
      break;
    case CameraMotionKind::Pan:
      target = eye + Rotate(target - eye, vertical, amount * radians);
      break;
    case CameraMotionKind::Tilt:
      target = eye + Rotate(target - eye, right, amount * radians);
      break;
    case CameraMotionKind::Roll:
      pose.roll += amount;
      break;
    case CameraMotionKind::Shake:
    case CameraMotionKind::Handheld: {
      const float t =
          seconds * effect.frequency * 2 * std::numbers::pi_v<float>;
      const float phase = static_cast<float>(effect.seed % 997) * 0.17f;
      offset = (right * std::sin(t + phase) +
                vertical * std::sin(t * 1.37f + phase * 2)) *
               amount;
      if (effect.kind == CameraMotionKind::Handheld) {
        offset = offset * 0.3f + (right * std::sin(t * 0.19f + phase) +
                                  vertical * std::sin(t * 0.27f + phase)) *
                                     amount * 0.7f;
        pose.roll += std::sin(t * 0.23f + phase) * amount * 8.0f;
      }
      break;
    }
    }
    eye = eye + offset;
    const bool translateTarget = !lookAt ||
                                 effect.kind == CameraMotionKind::Shake ||
                                 effect.kind == CameraMotionKind::Handheld;
    if (translateTarget)
      target = target + offset;
  }
  if (MATH::Length(target - eye) < 0.0001f)
    target = eye + MATH::Vec3{0, 0, 1};
  const auto forward = MATH::Normalize(target - eye);
  if (std::abs(MATH::Dot(forward, up)) > 0.999f)
    up = {0, 0, 1};
  up = Rotate(up, forward, pose.roll * radians);
  Camera3D camera;
  camera.SetPerspective(std::clamp(pose.fov, 1.0f, 179.0f) * radians, aspect,
                        shot.nearClip, shot.farClip);
  camera.SetLookAt(eye, target, up);
  return camera;
}
} // namespace HIKARI::SEQUENCER
