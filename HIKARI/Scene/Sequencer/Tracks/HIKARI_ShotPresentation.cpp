#include "HIKARI_ShotPresentation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>
#include <utility>

namespace HIKARI::SEQUENCER {
namespace {
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
constexpr MATH::Vec3 kForward{0.0f, 0.0f, 1.0f};
constexpr MATH::Vec3 kUp{0.0f, 1.0f, 0.0f};
float Safe(float v, float fallback = 0.0f) { return std::isfinite(v) ? v : fallback; }
MATH::Vec3 Safe(MATH::Vec3 v, MATH::Vec3 fallback = {}) {
    return {Safe(v.x, fallback.x), Safe(v.y, fallback.y), Safe(v.z, fallback.z)};
}
MATH::Vec3 Point(const MATH::Mat4& m, MATH::Vec3 p) {
    return {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
            m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
            m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2]};
}
MATH::Vec3 Direction(const MATH::Mat4& m, MATH::Vec3 v) { return Point(m, v) - Point(m, {}); }
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
void SanitizePose(ShotPose& pose) {
    pose.time = (std::max)(0.0f, Safe(pose.time));
    pose.position = Safe(pose.position, {0.0f, 2.0f, -8.0f});
    pose.rotation = MATH::NormalizeQ(
        {Safe(pose.rotation.x), Safe(pose.rotation.y), Safe(pose.rotation.z), Safe(pose.rotation.w, 1.0f)});
    pose.fov = pose.reference.IsValid() ? Safe(pose.fov) : std::clamp(Safe(pose.fov, 55.0f), 1.0f, 179.0f);
    pose.nearClip = (std::max)(0.001f, Safe(pose.nearClip, 0.1f));
    pose.farClip = (std::max)(pose.nearClip + 0.01f, Safe(pose.farClip, 2000));
    pose.inTangent = Safe(pose.inTangent);
    pose.outTangent = Safe(pose.outTangent);
    pose.inFovTangent = Safe(pose.inFovTangent);
    pose.outFovTangent = Safe(pose.outFovTangent);
    pose.customTangents &= 15u;
    for (auto& shape : pose.curveShapes)
        NormalizeScalarCurveShape(shape);
    if (!TEXT::IsNamedEnum(kShotInterpolationNames, pose.interpolation))
        pose.interpolation = ShotInterpolation::Spline;
}

// 世界の向きで a から b へ回す回転ベクトル（軸 × 角度）と、回転ベクトルから四元数。
MATH::Vec3 RotationBetween(const MATH::Quat& a, const MATH::Quat& b) {
    MATH::Quat r = MATH::MultiplyQ(b, {-a.x, -a.y, -a.z, a.w});
    if (r.w < 0.0f)
        r = {-r.x, -r.y, -r.z, -r.w};
    const MATH::Vec3 axis{r.x, r.y, r.z};
    const float sine = MATH::Length(axis);
    if (sine < 1.0e-7f)
        return axis * 2.0f;
    return axis * (2.0f * std::atan2(sine, r.w) / sine);
}
MATH::Quat RotationFromVector(const MATH::Vec3& v) {
    const float angle = MATH::Length(v);
    if (angle < 1.0e-7f)
        return MATH::NormalizeQ({v.x * 0.5f, v.y * 0.5f, v.z * 0.5f, 1.0f});
    const float scale = std::sin(angle * 0.5f) / angle;
    return {v.x * scale, v.y * scale, v.z * scale, std::cos(angle * 0.5f)};
}

// 键 i での時刻あたりの変化。途中の键は前後の键の差を時刻差で割り（間隔が不揃いでも速さがつながる）、
// 最初と最後の键は 0（そこで緩やかに止まる）。
template <class Value, class Difference>
Value KeyVelocity(std::span<const ShotPose> keys, size_t i, Difference difference) {
    if (i == 0 || i + 1 >= keys.size())
        return Value{};
    const float span = keys[i + 1].time - keys[i - 1].time;
    return span > 1.0e-5f ? difference(keys[i - 1], keys[i + 1]) * (1.0f / span) : Value{};
}
struct Hermite {
    float h00, h10, h01, h11;
    explicit Hermite(float t) {
        const float t2 = t * t, t3 = t2 * t;
        h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        h10 = t3 - 2.0f * t2 + t;
        h01 = -2.0f * t3 + 3.0f * t2;
        h11 = t3 - t2;
    }
};
float& PoseChannel(ShotPose& pose, unsigned channel) {
    return channel == 0 ? pose.position.x : channel == 1 ? pose.position.y : channel == 2 ? pose.position.z : pose.fov;
}
float PoseChannel(const ShotPose& pose, unsigned channel) {
    return channel == 0 ? pose.position.x : channel == 1 ? pose.position.y : channel == 2 ? pose.position.z : pose.fov;
}
float& PoseTangent(ShotPose& pose, unsigned channel, bool incoming) {
    auto& v = incoming ? pose.inTangent : pose.outTangent;
    return channel == 0   ? v.x
           : channel == 1 ? v.y
           : channel == 2 ? v.z
           : incoming     ? pose.inFovTangent
                          : pose.outFovTangent;
}
float ChannelSlope(std::span<const ShotPose> keys, size_t i, unsigned channel, bool incoming) {
    const auto& key = keys[i];
    if ((key.customTangents & (1u << channel)) && key.curveShapes[channel].tangent != ScalarTangentMode::Auto) {
        auto copy = key;
        return PoseTangent(copy, channel, incoming);
    }
    if ((incoming && i == 0) || (!incoming && i + 1 == keys.size()))
        return 0;
    const size_t from = incoming ? i - 1 : i;
    const auto mode = keys[from].interpolation;
    if (mode == ShotInterpolation::Linear)
        return (PoseChannel(keys[from + 1], channel) - PoseChannel(keys[from], channel)) /
               (keys[from + 1].time - keys[from].time);
    if (mode == ShotInterpolation::Spline ||
        ((key.customTangents & (1u << channel)) && key.curveShapes[channel].tangent == ScalarTangentMode::Auto))
        return KeyVelocity<float>(keys, i, [channel](const auto& a, const auto& b) {
            return PoseChannel(b, channel) - PoseChannel(a, channel);
        });
    return 0;
}
ShotPose InterpolateKeys(std::span<const ShotPose> keys, size_t i0, float seconds) {
    const auto& a = keys[i0];
    const auto& b = keys[i0 + 1];
    const float span = (std::max)(b.time - a.time, 1.0e-5f);
    const float t = std::clamp((seconds - a.time) / span, 0.0f, 1.0f);
    ShotPose pose = a;
    pose.time = seconds;
    const Hermite h(t);
    for (unsigned c = 0; c < 4; ++c) {
        if ((a.customTangents | b.customTangents) & (1u << c)) {
            ScalarCurveKey from{a.time, PoseChannel(a, c), 0, ChannelSlope(keys, i0, c, false)};
            ScalarCurveKey to{b.time, PoseChannel(b, c), ChannelSlope(keys, i0 + 1, c, true), 0};
            from.interpolation = (a.customTangents & (1u << c))               ? a.curveInterpolations[c]
                                 : a.interpolation == ShotInterpolation::Hold ? ScalarCurveInterpolation::Hold
                                                                              : ScalarCurveInterpolation::Cubic;
            from.shape = a.curveShapes[c];
            to.shape = b.curveShapes[c];
            PoseChannel(pose, c) = EvaluateScalarSegment(from, to, seconds);
        } else if (a.interpolation != ShotInterpolation::Hold)
            PoseChannel(pose, c) = PoseChannel(a, c) * h.h00 + ChannelSlope(keys, i0, c, false) * h.h10 * span +
                                   PoseChannel(b, c) * h.h01 + ChannelSlope(keys, i0 + 1, c, true) * h.h11 * span;
    }
    if (a.interpolation == ShotInterpolation::Hold)
        return pose;
    const float u = a.interpolation == ShotInterpolation::Smooth ? t * t * (3 - 2 * t) : t;
    pose.nearClip = a.nearClip + (b.nearClip - a.nearClip) * u;
    pose.farClip = a.farClip + (b.farClip - a.farClip) * u;
    if (a.interpolation != ShotInterpolation::Spline) {
        pose.rotation = MATH::SlerpQ(a.rotation, b.rotation, u);
        return pose;
    }
    const auto rotationDelta = [](const ShotPose& from, const ShotPose& to) {
        return RotationBetween(from.rotation, to.rotation);
    };
    const auto wa = KeyVelocity<MATH::Vec3>(keys, i0, rotationDelta);
    const auto wb = KeyVelocity<MATH::Vec3>(keys, i0 + 1, rotationDelta);
    const auto turn = RotationBetween(a.rotation, b.rotation);
    pose.rotation =
        MATH::MultiplyQ(RotationFromVector(wa * (h.h10 * span) + turn * h.h01 + wb * (h.h11 * span)), a.rotation);
    return pose;
}

// 一様な Catmull-Rom の区間 [p1, p2]。
MATH::Vec3 CatmullRom(MATH::Vec3 p0, MATH::Vec3 p1, MATH::Vec3 p2, MATH::Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
            (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) *
           0.5f;
}
} // namespace

MATH::Vec3 EvaluateShotRail(const ShotRail& rail, float t) {
    const auto& p = rail.points;
    if (p.empty())
        return {};
    if (p.size() == 1)
        return p.front();
    const size_t count = p.size();
    const size_t segments = rail.loop ? count : count - 1;
    const auto at = [&](ptrdiff_t index) {
        const auto n = static_cast<ptrdiff_t>(count);
        return p[static_cast<size_t>(rail.loop ? ((index % n) + n) % n : std::clamp(index, ptrdiff_t{0}, n - 1))];
    };
    const auto point = [&](size_t segment, float local) {
        const auto i = static_cast<ptrdiff_t>(segment);
        return CatmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), local);
    };
    // 道のりで割り当てる（点の間隔が不揃いでも railT の変化に対して等速に進む）。区間を細かく刻んだ折れ線の長さ。
    constexpr size_t kSteps = 16;
    std::vector<float> lengths(segments * kSteps + 1, 0.0f);
    MATH::Vec3 previous = p.front();
    for (size_t segment = 0; segment < segments; ++segment)
        for (size_t step = 1; step <= kSteps; ++step) {
            const MATH::Vec3 next = point(segment, static_cast<float>(step) / kSteps);
            const size_t index = segment * kSteps + step;
            lengths[index] = lengths[index - 1] + MATH::Length(next - previous);
            previous = next;
        }
    const float total = lengths.back();
    if (total <= 1.0e-6f)
        return p.front();
    const float distance = std::clamp(Safe(t), 0.0f, 1.0f) * total;
    const auto found = std::lower_bound(lengths.begin(), lengths.end(), distance);
    const size_t index = (std::clamp)(static_cast<size_t>(found - lengths.begin()), size_t{1}, lengths.size() - 1);
    const float lo = lengths[index - 1], hi = lengths[index];
    const float fraction = hi > lo ? std::clamp((distance - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const float step = static_cast<float>((index - 1) % kSteps) + fraction;
    return point((index - 1) / kSteps, step / kSteps);
}

float ShotMaterialTime(const ShotCamera& shot, float seconds) {
    return shot.timing.keys.empty() ? Safe(seconds) : Safe(shot.timing.Evaluate(seconds));
}

ShotPose EvaluateShotPose(const ShotCamera& shot, float seconds, std::span<const ShotPose> resolvedKeys,
                          const ShotPose* resolvedBase) {
    seconds = Safe(seconds);
    const std::span<const ShotPose> keys = resolvedKeys.empty() ? std::span<const ShotPose>{shot.keys} : resolvedKeys;
    ShotPose pose = resolvedBase ? *resolvedBase : shot.base;
    if (!keys.empty()) {
        if (seconds <= keys.front().time)
            pose = keys.front();
        else if (seconds >= keys.back().time)
            pose = keys.back();
        else {
            const auto next = std::upper_bound(keys.begin(), keys.end(), seconds,
                                               [](float value, const ShotPose& key) { return value < key.time; });
            pose = InterpolateKeys(keys, static_cast<size_t>(next - keys.begin()) - 1, seconds);
        }
    }
    pose.time = seconds;
    if (shot.rail.points.size() >= 2)
        pose.position = EvaluateShotRail(shot.rail, shot.railT.Evaluate(seconds));
    return pose;
}

ScalarCurve GetShotPoseCurve(const ShotCamera& shot, unsigned channel) {
    channel = (std::min)(channel, 3u);
    ScalarCurve result{PoseChannel(shot.base, channel)};
    for (size_t i = 0; i < shot.keys.size(); ++i)
        result.keys.push_back(
            {shot.keys[i].time, PoseChannel(shot.keys[i], channel), ChannelSlope(shot.keys, i, channel, true),
             ChannelSlope(shot.keys, i, channel, false),
             (shot.keys[i].customTangents & (1u << channel))         ? shot.keys[i].curveInterpolations[channel]
             : shot.keys[i].interpolation == ShotInterpolation::Hold ? ScalarCurveInterpolation::Hold
                                                                     : ScalarCurveInterpolation::Cubic,
             shot.keys[i].curveShapes[channel]});
    return result;
}
void SetShotPoseCurve(ShotCamera& shot, unsigned channel, const ScalarCurve& curve) {
    channel = (std::min)(channel, 3u);
    const auto before = shot;
    // 他軸の値は同じ素材時刻で採り直す。曲線編集は timing による再時刻化を二重適用しない。
    auto source = before;
    source.timing.keys.clear();
    shot.keys.clear();
    for (size_t i = 0; i < curve.keys.size(); ++i) {
        const auto& key = curve.keys[i];
        auto pose = curve.keys.size() == before.keys.size() ? before.keys[i] : EvaluateShotPose(source, key.time);
        pose.time = key.time;
        PoseChannel(pose, channel) = key.value;
        PoseTangent(pose, channel, true) = key.inSlope;
        PoseTangent(pose, channel, false) = key.outSlope;
        pose.customTangents |= 1u << channel;
        pose.curveShapes[channel] = key.shape;
        pose.curveInterpolations[channel] = key.interpolation;
        shot.keys.push_back(pose);
    }
    PoseChannel(shot.base, channel) = curve.value;
    NormalizeShotCamera(shot);
}

const ShotPose* FindShotPoseKey(const ShotCamera& shot, float time) noexcept {
    for (const auto& key : shot.keys)
        if (std::abs(key.time - time) < kShotPoseKeyEpsilon)
            return &key;
    return nullptr;
}

ShotPose* FindShotPoseKey(ShotCamera& shot, float time) noexcept {
    return const_cast<ShotPose*>(FindShotPoseKey(std::as_const(shot), time));
}

void SetShotPose(ShotCamera& shot, const ShotPose& pose, bool keyed) {
    ShotPose value = pose;
    SanitizePose(value);
    if (!keyed && shot.keys.empty()) {
        shot.base = value;
        shot.base.time = 0.0f;
        return;
    }
    if (auto* existing = FindShotPoseKey(shot, value.time))
        *existing = value;
    else
        shot.keys.push_back(value);
    NormalizeShotCamera(shot);
}

bool MoveShotPoseKey(ShotCamera& shot, float time, float newTime) {
    auto* key = FindShotPoseKey(shot, time);
    if (key == nullptr)
        return false;
    newTime = (std::max)(0.0f, Safe(newTime));
    if (const auto* occupied = FindShotPoseKey(shot, newTime); occupied != nullptr && occupied != key)
        return false;
    key->time = newTime;
    NormalizeShotCamera(shot);
    return true;
}

bool RemoveShotPoseKey(ShotCamera& shot, float time) {
    const ShotPose before = EvaluateShotPose(shot, time);
    const auto removed =
        std::erase_if(shot.keys, [&](const ShotPose& key) { return std::abs(key.time - time) < kShotPoseKeyEpsilon; });
    if (removed == 0)
        return false;
    if (shot.keys.empty()) {
        shot.base = before;
        shot.base.time = 0.0f;
    }
    NormalizeShotCamera(shot);
    return true;
}

void NormalizeShotCamera(ShotCamera& shot) {
    SanitizePose(shot.base);
    shot.base.time = 0.0f;
    for (auto& key : shot.keys)
        SanitizePose(key);
    std::stable_sort(shot.keys.begin(), shot.keys.end(),
                     [](const ShotPose& a, const ShotPose& b) { return a.time < b.time; });
    // 同じ時刻の键は後に置いた方を残す。
    size_t count = 0;
    for (const auto& key : shot.keys) {
        if (count && std::abs(shot.keys[count - 1].time - key.time) < kShotPoseKeyEpsilon)
            shot.keys[count - 1] = key;
        else
            shot.keys[count++] = key;
    }
    shot.keys.resize(count);
    for (auto& point : shot.rail.points)
        point = Safe(point);
    NormalizeScalarCurve(shot.timing);
    NormalizeScalarCurve(shot.railT);
    NormalizeScalarCurve(shot.aimWeight);
    shot.targetOffset = Safe(shot.targetOffset);
    shot.pivotDistance = std::clamp(Safe(shot.pivotDistance, 5.0f), 0.1f, 1000.0f);
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

MATH::Vec3 ShotPoseForward(const ShotPose& pose) { return MATH::RotateVector(pose.rotation, kForward); }

MATH::Vec3 ShotPoseUp(const ShotPose& pose) { return MATH::RotateVector(pose.rotation, kUp); }

MATH::Quat ShotRotationLookingAlong(const MATH::Vec3& forward, const MATH::Quat& previous) {
    const MATH::Vec3 oldForward = MATH::RotateVector(previous, kForward);
    const MATH::Vec3 oldUp = MATH::RotateVector(previous, kUp);
    const auto oldLevel = MATH::BuildViewBasisRH(oldForward, kUp);
    const float roll = std::atan2(MATH::Dot(oldUp, oldLevel.right), MATH::Dot(oldUp, oldLevel.up));
    const auto level = MATH::BuildViewBasisRH(forward, kUp);
    return MATH::QuatFromForwardUp(forward, level.up * std::cos(roll) + level.right * std::sin(roll));
}

ShotPose CaptureShotPose(const Camera3D& camera, float seconds, const MATH::Mat4* anchor) {
    ShotPose pose;
    pose.time = seconds;
    pose.position = camera.GetPosition();
    pose.fov = camera.GetFovYRad() / radians;
    pose.nearClip = camera.GetNearZ();
    pose.farClip = camera.GetFarZ();
    MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
    MATH::Vec3 up = camera.GetUp();
    if (anchor) {
        const auto inverse = MATH::Inverse(*anchor);
        pose.position = Point(inverse, pose.position);
        forward = Direction(inverse, forward);
        up = Direction(inverse, up);
    }
    pose.rotation = MATH::QuatFromForwardUp(forward, up);
    return pose;
}

Camera3D EvaluateShotCamera(const ShotCamera& shot, float seconds, float aspect, const MATH::Mat4* anchor,
                            const MATH::Vec3* lookAt, std::span<const ShotPose> resolvedKeys,
                            const ShotPose* resolvedBase, bool includeMotion) {
    const ShotPose pose = EvaluateShotPose(shot, ShotMaterialTime(shot, seconds), resolvedKeys, resolvedBase);
    seconds = pose.time;
    MATH::Vec3 eye = pose.position;
    MATH::Vec3 forward = ShotPoseForward(pose);
    MATH::Vec3 up = ShotPoseUp(pose);
    if (anchor) {
        eye = Point(*anchor, eye);
        forward = MATH::Normalize(Direction(*anchor, forward));
        up = MATH::Normalize(Direction(*anchor, up));
    }
    MATH::Vec3 target = eye + forward * shot.pivotDistance;
    if (lookAt) {
        // 瞄准：相手の方向へ向きを寄せる（aimWeight で姿勢键の向きと混ぜ、傾きは姿勢键の上向きから取る）。
        const MATH::Vec3 aimTarget = *lookAt + shot.targetOffset;
        const MATH::Vec3 toTarget = aimTarget - eye;
        const float weight = std::clamp(shot.aimWeight.Evaluate(seconds), 0.0f, 1.0f);
        if (MATH::Length(toTarget) > 1.0e-4f && weight > 0.0f) {
            const MATH::Quat aimed =
                MATH::SlerpQ(MATH::QuatFromForwardUp(forward, up), MATH::QuatFromForwardUp(toTarget, up), weight);
            forward = MATH::RotateVector(aimed, kForward);
            up = MATH::RotateVector(aimed, kUp);
            target = weight >= 1.0f ? aimTarget : eye + forward * MATH::Length(toTarget);
        }
    }
    float fov = pose.fov;
    if (includeMotion)
        ApplyCameraMotionEffects(shot.motion, seconds, eye, target, up, fov, 0, 1, lookAt != nullptr);
    if (MATH::Length(target - eye) < 0.0001f)
        target = eye + kForward;
    Camera3D camera;
    camera.SetPerspective(std::clamp(fov, 1.0f, 179.0f) * radians, aspect, pose.nearClip, pose.farClip);
    camera.SetLookAt(eye, target, up);
    return camera;
}
} // namespace HIKARI::SEQUENCER
