#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {
namespace {
bool SameTime(float a, float b) { return std::abs(a - b) < 1.e-5f; }
template <class Key>
Key& KeyAt(std::vector<Key>& keys, float time) {
    for (auto& key : keys)
        if (SameTime(key.timeSeconds, time))
            return key;
    Key next{};
    // 新しい鍵だけに左側の補間方式を継承し、既存の鍵の値・ID は触らない。
    uint64_t id = 1;
    for (const auto& key : keys) {
        id = (std::max)(id, key.id + 1);
        if (key.timeSeconds < time)
            next.interpolation = key.interpolation;
    }
    next.id = id;
    next.timeSeconds = time;
    return *keys.insert(
        std::lower_bound(keys.begin(), keys.end(), time, [](const Key& key, float t) { return key.timeSeconds < t; }),
        next);
}
} // namespace
bool ReadBoundCameraPose(const CinematicSequence& sequence, SEQUENCER::SequenceBindingId id, float time,
                         BoundCameraPose& pose) {
    const auto transform = SEQUENCER::EvaluateCameraTransformTrack(sequence.cameraTransformTrack, id, time);
    const auto lens = SEQUENCER::EvaluateCameraLensTrack(sequence.cameraLensTrack, id, time);
    if (!transform.valid || !lens.valid)
        return false;
    pose = {transform.position, MATH::EulerXYZDegreesFromQuat(transform.rotation), lens.verticalFovDegrees,
            lens.nearClip, lens.farClip};
    // 鍵上では Euler の同値表現へ変換しない。180 度付近の作者値もそのまま表示する。
    if (const auto* channel = SEQUENCER::FindCameraTransformChannel(sequence.cameraTransformTrack, id))
        for (const auto& key : channel->keyframes)
            if (SameTime(key.timeSeconds, time))
                pose.rotationEulerDeg = key.rotationEulerDeg;
    return true;
}
bool WriteBoundCameraPose(CinematicSequence& sequence, SEQUENCER::SequenceBindingId id, float time,
                          const BoundCameraPose& pose, uint32_t parts) {
    BoundCameraPose current;
    if (!ReadBoundCameraPose(sequence, id, time, current) || !std::isfinite(time) || time < 0 ||
        !MATH::IsFinite(pose.position) || !MATH::IsFinite(pose.rotationEulerDeg) || !MATH::IsFinite(pose.fov))
        return false;
    if (parts & (BoundPosition | BoundRotation)) {
        auto* channel = SEQUENCER::FindCameraTransformChannel(sequence.cameraTransformTrack, id);
        const bool exists = std::any_of(channel->keyframes.begin(), channel->keyframes.end(),
                                        [time](const auto& k) { return SameTime(k.timeSeconds, time); });
        auto& key = KeyAt(channel->keyframes, time);
        if (!exists) {
            key.position = current.position;
            key.rotationEulerDeg = current.rotationEulerDeg;
        }
        if (parts & BoundPosition)
            key.position = pose.position;
        if (parts & BoundRotation)
            key.rotationEulerDeg = pose.rotationEulerDeg;
    }
    if (parts & BoundFov) {
        auto& key = KeyAt(SEQUENCER::FindCameraLensChannel(sequence.cameraLensTrack, id)->keyframes, time);
        key.verticalFovDegrees = std::clamp(pose.fov, 1.f, 179.f);
        key.nearClip = current.nearClip;
        key.farClip = current.farClip;
    }
    return true;
}
std::vector<float> BoundCameraKeyTimes(const CinematicSequence& sequence, SEQUENCER::SequenceBindingId id) {
    std::vector<float> times;
    if (const auto* c = SEQUENCER::FindCameraTransformChannel(sequence.cameraTransformTrack, id))
        for (const auto& k : c->keyframes)
            times.push_back(k.timeSeconds);
    if (const auto* c = SEQUENCER::FindCameraLensChannel(sequence.cameraLensTrack, id))
        for (const auto& k : c->keyframes)
            times.push_back(k.timeSeconds);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), SameTime), times.end());
    return times;
}
bool MoveBoundCameraKey(CinematicSequence& sequence, SEQUENCER::SequenceBindingId id, float from, float to) {
    if (!std::isfinite(to) || to < 0 || SameTime(from, to))
        return false;
    const auto times = BoundCameraKeyTimes(sequence, id);
    if (std::any_of(times.begin(), times.end(), [to](float t) { return SameTime(t, to); }))
        return false;
    bool changed = false;
    const auto move = [&](auto* c) {
        if (!c)
            return;
        for (auto& k : c->keyframes)
            if (SameTime(k.timeSeconds, from)) {
                k.timeSeconds = to;
                changed = true;
            }
        std::sort(c->keyframes.begin(), c->keyframes.end(),
                  [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    };
    move(SEQUENCER::FindCameraTransformChannel(sequence.cameraTransformTrack, id));
    move(SEQUENCER::FindCameraLensChannel(sequence.cameraLensTrack, id));
    return changed;
}
bool RemoveBoundCameraKey(CinematicSequence& sequence, SEQUENCER::SequenceBindingId id, float time) {
    bool changed = false;
    const auto remove = [&](auto* c) {
        if (c && c->keyframes.size() > 1)
            changed |=
                std::erase_if(c->keyframes, [time](const auto& k) { return SameTime(k.timeSeconds, time); }) != 0;
    };
    remove(SEQUENCER::FindCameraTransformChannel(sequence.cameraTransformTrack, id));
    remove(SEQUENCER::FindCameraLensChannel(sequence.cameraLensTrack, id));
    return changed;
}
} // namespace HIKARI::EDITOR
