#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {
void ApplyResolvedShotPoseCurves(SEQUENCER::ShotCamera& camera, const SEQUENCER::ShotCamera& resolved,
                                 const std::array<ScalarCurve, 4>& curves) {
    std::array<ScalarCurve, 4> before;
    for (unsigned c = 0; c < 4; ++c)
        before[c] = SEQUENCER::GetShotPoseCurve(resolved, c);
    auto edited = camera;
    edited.keys.clear();
    // 同時に編集した XYZ/FOV を一つの姿勢へ戻す。先に書いた軸で他軸のキーを再生成しない。
    for (size_t i = 0; i < curves[0].keys.size(); ++i) {
        const float time = curves[0].keys[i].time;
        size_t old = camera.keys.size();
        if (curves[0].keys.size() == camera.keys.size())
            old = i;
        else
            for (size_t j = 0; j < camera.keys.size(); ++j)
                if (std::abs(camera.keys[j].time - time) < SEQUENCER::kShotPoseKeyEpsilon) {
                    old = j;
                    break;
                }
        auto pose = old < camera.keys.size() ? camera.keys[old] : SEQUENCER::EvaluateShotPose(resolved, time);
        if (old == camera.keys.size())
            pose.reference = {};
        pose.time = time;
        for (unsigned c = 0; c < 4; ++c) {
            if (i >= curves[c].keys.size())
                continue;
            const auto& key = curves[c].keys[i];
            auto oldKey = old < before[c].keys.size() ? before[c].keys[old] : key;
            oldKey.time = key.time;
            if (old < camera.keys.size() && key == oldKey)
                continue;
            float& value = c == 0 ? pose.position.x : c == 1 ? pose.position.y : c == 2 ? pose.position.z : pose.fov;
            value = key.value + (pose.reference.IsValid() ? value - oldKey.value : 0);
            float& in = c == 0   ? pose.inTangent.x
                        : c == 1 ? pose.inTangent.y
                        : c == 2 ? pose.inTangent.z
                                 : pose.inFovTangent;
            float& out = c == 0   ? pose.outTangent.x
                         : c == 1 ? pose.outTangent.y
                         : c == 2 ? pose.outTangent.z
                                  : pose.outFovTangent;
            in = key.inSlope;
            out = key.outSlope;
            pose.customTangents |= 1u << c;
            pose.curveShapes[c] = key.shape;
            pose.curveInterpolations[c] = key.interpolation;
        }
        edited.keys.push_back(pose);
    }
    camera = std::move(edited);
    SEQUENCER::NormalizeShotCamera(camera);
}
uint64_t SplitSequenceShot(CinematicSequence& sequence, uint64_t shotId, float sequenceTime) {
    auto& clips = sequence.cameraCutTrack.clips;
    const auto found = std::find_if(clips.begin(), clips.end(), [=](const auto& clip) { return clip.id == shotId; });
    if (found == clips.end() || !std::isfinite(sequenceTime))
        return 0;
    const float at = sequenceTime - found->startTimeSeconds;
    if (at < SEQUENCER::kMinCameraCutClipDurationSeconds ||
        found->durationSeconds - at < SEQUENCER::kMinCameraCutClipDurationSeconds)
        return 0;
    // 遷移の途中では前片段の画を参照するため、そこで分割して境界を増やさない。
    if (found->transition.mode == SEQUENCER::CameraCutTransitionMode::EaseInOut &&
        at < found->transition.durationSeconds)
        return 0;
    auto second = *found;
    second.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    second.startTimeSeconds = sequenceTime;
    second.durationSeconds -= at;
    second.sourceOffsetSeconds += at;
    second.transition = {};
    second.camera.name += " 2";
    found->durationSeconds = at;
    const auto id = second.id;
    clips.push_back(std::move(second));
    return id;
}
uint64_t DuplicateSequenceShot(CinematicSequence& sequence, uint64_t shotId) {
    auto& clips = sequence.cameraCutTrack.clips;
    const auto found = std::find_if(clips.begin(), clips.end(), [=](const auto& clip) { return clip.id == shotId; });
    if (found == clips.end())
        return 0;
    auto copy = *found;
    copy.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    copy.camera.name += " Copy";
    const float end = found->startTimeSeconds + found->durationSeconds;
    float latest = 0;
    bool occupied = false;
    for (const auto& other : clips) {
        latest = (std::max)(latest, other.startTimeSeconds + other.durationSeconds);
        occupied |= other.id != found->id && other.startTimeSeconds < end + copy.durationSeconds &&
                    other.startTimeSeconds + other.durationSeconds > end;
    }
    copy.startTimeSeconds = occupied ? latest : end;
    sequence.durationSeconds = (std::max)(sequence.durationSeconds, copy.startTimeSeconds + copy.durationSeconds);
    const auto id = copy.id;
    clips.push_back(std::move(copy));
    return id;
}
} // namespace HIKARI::EDITOR
