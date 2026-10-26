#include "HIKARI_ObjectTrack.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {
namespace {
bool Finite(const MATH::Vec3& v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Finite(const MATH::Quat& q) noexcept {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
MATH::Quat Unit(const MATH::Quat& q) noexcept {
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(length) || length < 1.0e-6f)
        return MATH::Quat::Identity();
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}
// 键 index の位置の速さ（一秒あたり）。両隣があれば前後の差、端は 0（静かに動き出して静かに止まる）。
MATH::Vec3 Tangent(const std::vector<ObjectTrackKey>& keys, size_t index) noexcept {
    if (index == 0 || index + 1 >= keys.size())
        return {};
    const float span = keys[index + 1].time - keys[index - 1].time;
    if (span <= 1.0e-5f)
        return {};
    return (keys[index + 1].position - keys[index - 1].position) * (1.0f / span);
}
} // namespace

void NormalizeObjectTracks(std::vector<ObjectTrack>& tracks) {
    for (auto& track : tracks) {
        for (auto& key : track.keys) {
            if (!std::isfinite(key.time) || key.time < 0.0f)
                key.time = 0.0f;
            if (!Finite(key.position))
                key.position = {};
            key.rotation = Finite(key.rotation) ? Unit(key.rotation) : MATH::Quat::Identity();
            if (!Finite(key.scale))
                key.scale = {1.0f, 1.0f, 1.0f};
            if (!std::isfinite(key.light) || key.light < 0.0f)
                key.light = 1.0f;
        }
        std::stable_sort(track.keys.begin(), track.keys.end(),
                         [](const ObjectTrackKey& a, const ObjectTrackKey& b) { return a.time < b.time; });
    }
}

bool ValidateObjectTracks(const std::vector<ObjectTrack>& tracks, std::string& error) {
    error.clear();
    if (tracks.size() > kMaximumObjectTracks) {
        error = "Object tracks exceed 128";
        return false;
    }
    std::unordered_set<uint64_t> ids;
    for (const auto& track : tracks) {
        const auto invalid = [&](const char* message) {
            error = "Object track " + std::to_string(track.id) + ": " + message;
            return false;
        };
        if (track.id == 0u || !ids.insert(track.id).second)
            return invalid("duplicate or empty id");
        if (track.keys.size() > kMaximumObjectTrackKeys)
            return invalid("too many keys");
        for (const auto& key : track.keys) {
            if (!std::isfinite(key.time) || key.time < 0.0f || key.time > kMaxSequenceDurationSeconds)
                return invalid("invalid key time");
            if (!Finite(key.position) || !Finite(key.rotation) || !Finite(key.scale) || !std::isfinite(key.light) ||
                key.light < 0.0f)
                return invalid("invalid key value");
            if (key.interpolation > ObjectKeyInterpolation::Step)
                return invalid("unknown interpolation");
        }
    }
    return true;
}

float GetObjectTracksContentEnd(const std::vector<ObjectTrack>& tracks) noexcept {
    float end = 0.0f;
    for (const auto& track : tracks)
        for (const auto& key : track.keys)
            if (std::isfinite(key.time))
                end = (std::max)(end, key.time);
    return end;
}

bool SampleObjectTrack(const ObjectTrack& track, float timeSeconds, ObjectTrackSample& out) noexcept {
    const auto& keys = track.keys;
    if (keys.empty())
        return false;
    const auto fromKey = [&out](const ObjectTrackKey& key) {
        out.position = key.position;
        out.rotation = key.rotation;
        out.scale = key.scale;
        out.light = key.light;
        return true;
    };
    if (timeSeconds <= keys.front().time)
        return fromKey(keys.front());
    if (timeSeconds >= keys.back().time)
        return fromKey(keys.back());
    // timeSeconds を挟む键（keys[index] <= t < keys[index + 1]）。
    const auto next = std::upper_bound(keys.begin(), keys.end(), timeSeconds,
                                       [](float t, const ObjectTrackKey& key) { return t < key.time; });
    const size_t index = static_cast<size_t>(std::distance(keys.begin(), next)) - 1u;
    const auto& a = keys[index];
    const auto& b = keys[index + 1];
    const float span = b.time - a.time;
    const float s = span > 1.0e-6f ? std::clamp((timeSeconds - a.time) / span, 0.0f, 1.0f) : 1.0f;
    float weight = s;
    switch (a.interpolation) {
    case ObjectKeyInterpolation::Step: weight = 0.0f; break;
    case ObjectKeyInterpolation::Ease: weight = s * s * (3.0f - 2.0f * s); break;
    default: break;
    }
    if (a.interpolation == ObjectKeyInterpolation::Smooth) {
        // Hermite 曲線：键の位置を通り、键での速さは前後の键から決める（端の键では 0）。
        const float s2 = s * s, s3 = s2 * s;
        const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f, h10 = s3 - 2.0f * s2 + s;
        const float h01 = -2.0f * s3 + 3.0f * s2, h11 = s3 - s2;
        out.position = a.position * h00 + Tangent(keys, index) * (h10 * span) + b.position * h01 +
                       Tangent(keys, index + 1) * (h11 * span);
    } else {
        out.position = MATH::Lerp(a.position, b.position, weight);
    }
    out.rotation = MATH::SlerpQ(a.rotation, b.rotation, weight);
    out.scale = MATH::Lerp(a.scale, b.scale, weight);
    out.light = a.light + (b.light - a.light) * weight;
    return true;
}

const char* ToString(ObjectKeyInterpolation interpolation) noexcept {
    switch (interpolation) {
    case ObjectKeyInterpolation::Linear: return "linear";
    case ObjectKeyInterpolation::Ease: return "ease";
    case ObjectKeyInterpolation::Step: return "step";
    default: return "smooth";
    }
}

bool ParseObjectKeyInterpolation(const std::string& text, ObjectKeyInterpolation& out) noexcept {
    if (text == "smooth")
        out = ObjectKeyInterpolation::Smooth;
    else if (text == "linear")
        out = ObjectKeyInterpolation::Linear;
    else if (text == "ease")
        out = ObjectKeyInterpolation::Ease;
    else if (text == "step")
        out = ObjectKeyInterpolation::Step;
    else
        return false;
    return true;
}
} // namespace HIKARI::SEQUENCER
