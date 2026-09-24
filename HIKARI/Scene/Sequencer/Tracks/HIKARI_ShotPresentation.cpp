#include "HIKARI_ShotPresentation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace HIKARI::SEQUENCER {
namespace {
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
float Safe(float v, float fallback = 0.0f) { return std::isfinite(v) ? v : fallback; }
MATH::Vec3 Rotate(MATH::Vec3 v, MATH::Vec3 axis, float angle) {
    axis = MATH::Normalize(axis);
    return v * std::cos(angle) + MATH::Cross(axis, v) * std::sin(angle) +
           axis * (MATH::Dot(axis, v) * (1.0f - std::cos(angle)));
}
MATH::Vec3 Point(const MATH::Mat4& m, MATH::Vec3 p) {
    return {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
            m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
            m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2]};
}
template <class T>
void Ids(std::vector<T>& items) {
    std::unordered_set<uint64_t> ids;
    uint64_t next = 1;
    for (auto& item : items) {
        if (!item.id || !ids.insert(item.id).second) {
            while (ids.contains(next))
                ++next;
            item.id = next;
            ids.insert(next++);
        }
    }
}
} // namespace

ShotPose EvaluateShotPose(const ShotCamera& shot, float seconds) {
    const auto value = [&](size_t i) { return shot.channels[i].Evaluate(seconds); };
    return {seconds, {value(0), value(1), value(2)}, {value(3), value(4), value(5)}, value(6), value(7)};
}

void SetShotPose(ShotCamera& shot, const ShotPose& pose, bool keyed) {
    const std::array values{pose.eye.x,    pose.eye.y,    pose.eye.z, pose.target.x,
                            pose.target.y, pose.target.z, pose.fov,   pose.roll};
    for (size_t i = 0; i < values.size(); ++i) {
        if (keyed || !shot.channels[i].keys.empty())
            SetScalarCurveKey(shot.channels[i], pose.time, values[i]);
        else
            shot.channels[i].value = values[i];
    }
}

void NormalizeShotCamera(ShotCamera& shot) {
    for (auto& channel : shot.channels)
        NormalizeScalarCurve(channel);
    auto& fov = shot.channels[static_cast<size_t>(ShotChannel::Fov)];
    fov.value = std::clamp(fov.value, 1.0f, 179.0f);
    for (auto& key : fov.keys)
        key.value = std::clamp(key.value, 1.0f, 179.0f);
    shot.targetOffset = {Safe(shot.targetOffset.x), Safe(shot.targetOffset.y), Safe(shot.targetOffset.z)};
    shot.nearClip = (std::max)(0.001f, Safe(shot.nearClip, 0.1f));
    shot.farClip = (std::max)(shot.nearClip + 0.01f, Safe(shot.farClip, 2000));
    NormalizeCameraMotionEffects(shot.motion);
}

void NormalizeScreenEffects(std::vector<SequenceScreenEffect>& effects) {
    Ids(effects);
    for (auto& e : effects) {
        e.start = (std::max)(0.0f, Safe(e.start));
        e.duration = (std::max)(0.01f, Safe(e.duration, 3));
        e.fadeIn = std::clamp(Safe(e.fadeIn), 0.0f, e.duration);
        e.fadeOut = std::clamp(Safe(e.fadeOut), 0.0f, e.duration);
        NormalizeScalarCurve(e.amount);
        NormalizeScalarCurve(e.weight);
        e.color = {Safe(e.color.x, 1), Safe(e.color.y, 1), Safe(e.color.z, 1)};
    }
}

void SampleScreenEffects(const std::vector<SequenceScreenEffect>& effects, float seconds,
                         std::vector<ScreenEffectSample>& output) {
    for (const auto& e : effects) {
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

ShotPose CaptureShotPose(const Camera3D& camera, float seconds, const MATH::Mat4* anchor) {
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
    pose.roll = std::atan2(MATH::Dot(cameraUp, basis.right), MATH::Dot(cameraUp, basis.up)) / radians;
    return pose;
}

Camera3D EvaluateShotCamera(const ShotCamera& shot, float seconds, float aspect, const MATH::Mat4* anchor,
                            const MATH::Vec3* lookAt) {
    ShotPose pose = EvaluateShotPose(shot, seconds);
    auto eye = pose.eye, target = pose.target;
    MATH::Vec3 up{0, 1, 0};
    if (anchor) {
        eye = Point(*anchor, eye);
        target = Point(*anchor, target);
        up = MATH::Normalize(Point(*anchor, up) - Point(*anchor, {}));
    }
    if (lookAt)
        target = *lookAt + shot.targetOffset;
    ApplyCameraMotionEffects(shot.motion, seconds, eye, target, up, pose.fov, 0, 1, lookAt != nullptr);
    if (MATH::Length(target - eye) < 0.0001f)
        target = eye + MATH::Vec3{0, 0, 1};
    const auto forward = MATH::Normalize(target - eye);
    if (std::abs(MATH::Dot(forward, up)) > 0.999f)
        up = {0, 0, 1};
    up = Rotate(up, forward, pose.roll * radians);
    Camera3D camera;
    camera.SetPerspective(std::clamp(pose.fov, 1.0f, 179.0f) * radians, aspect, shot.nearClip, shot.farClip);
    camera.SetLookAt(eye, target, up);
    return camera;
}
} // namespace HIKARI::SEQUENCER
