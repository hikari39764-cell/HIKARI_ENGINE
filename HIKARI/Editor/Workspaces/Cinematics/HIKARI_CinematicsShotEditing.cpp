#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
// 曲線 first を at 秒で二つに分け、後半を second に置く（後半は at を 0 にずらす）。両側の境目に分ける時刻の値を
// 键として残す。
void SplitCurve(ScalarCurve& first, ScalarCurve& second, float at) {
    second = first;
    if (first.keys.empty())
        return;
    const float boundary = first.Evaluate(at);
    std::erase_if(first.keys, [&](const ScalarCurveKey& key) { return key.time > at; });
    std::erase_if(second.keys, [&](const ScalarCurveKey& key) { return key.time < at; });
    for (auto& key : second.keys)
        key.time -= at;
    SetScalarCurveKey(first, at, boundary);
    SetScalarCurveKey(second, 0.0f, boundary);
}

// 镜头の中の相対時刻を持つ効果（运动効果・镜头の画面効果）を at 秒で分ける。前半は at の後に始まるものを外し、
// 後半は at の前に終わるものを外して開始を at だけ前へずらす（期間全体の効果は両方に残す）。
template <class Effect>
void SplitTimedEffects(std::vector<Effect>& first, std::vector<Effect>& second, float at,
                       float (*duration)(const Effect&)) {
    std::erase_if(first, [&](const Effect& effect) { return effect.start >= at; });
    std::erase_if(second, [&](const Effect& effect) {
        const float span = duration(effect);
        return span > 0.0f && effect.start + span <= at;
    });
    for (auto& effect : second)
        effect.start = (std::max)(0.0f, effect.start - at);
}
} // namespace

float CinematicsWorkspaceController::ShotLocalTime(const SEQUENCER::CameraCutClip& shot) const {
    return std::clamp(cursor_.GetTimeSeconds() - shot.startTimeSeconds, 0.0f, shot.durationSeconds);
}

void CinematicsWorkspaceController::CreateShot(CinematicSequence& sequence, const Camera3D& camera) {
    SEQUENCER::CameraCutClip shot;
    shot.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    shot.startTimeSeconds = cursor_.GetTimeSeconds();
    for (const auto& existing : sequence.cameraCutTrack.clips)
        if (shot.startTimeSeconds >= existing.startTimeSeconds &&
            shot.startTimeSeconds < existing.startTimeSeconds + existing.durationSeconds)
            shot.startTimeSeconds = existing.startTimeSeconds + existing.durationSeconds;
    shot.durationSeconds = 3.0f;
    shot.camera.owned = true;
    shot.camera.name = "Shot " + std::to_string(sequence.cameraCutTrack.clips.size() + 1);
    SEQUENCER::SetShotPose(shot.camera, SEQUENCER::CaptureShotPose(camera), false);
    shot.camera.nearClip = camera.GetNearZ();
    shot.camera.farClip = camera.GetFarZ();
    sequence.cameraCutTrack.clips.push_back(shot);
    selectedId_ = shot.id;
    selectionKind_ = SelectionKind::Shot;
    previewEnabled_ = true;
    previewChanged_ = true;
    sequence.durationSeconds = (std::max)(sequence.durationSeconds, shot.startTimeSeconds + shot.durationSeconds);
    cursor_.SetDuration(sequence.durationSeconds);
    cursor_.Seek(shot.startTimeSeconds);
}

SEQUENCER::ShotPose CinematicsWorkspaceController::CaptureShotPoseFor(DocumentSceneBase& scene,
                                                                      CinematicSequence& sequence,
                                                                      const SEQUENCER::CameraCutClip& shot,
                                                                      const Camera3D& camera, float keyTime) const {
    SceneObjectId anchorId;
    const auto* anchor = previewBindings_.Resolve(sequence.bindings, shot.camera.anchor, anchorId)
                             ? scene.GetWorld().FindObject(anchorId)
                             : nullptr;
    const auto anchorMatrix = anchor ? anchor->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
    return SEQUENCER::CaptureShotPose(camera, keyTime, anchor ? &anchorMatrix : nullptr);
}

void CinematicsWorkspaceController::WriteShotPose(CinematicSequence& sequence, SEQUENCER::CameraCutClip& shot,
                                                  const SEQUENCER::ShotPose& pose, uint32_t parts) {
    // 外部軌道は元の補間と未編集フィールドを保ったまま、その時刻の鍵だけを更新する。
    if (!shot.camera.owned) {
        BoundCameraPose value;
        const float time = shot.startTimeSeconds + pose.time;
        if (!ReadBoundCameraPose(sequence, shot.cameraBindingId, time, value))
            return;
        value.position = pose.position;
        value.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(pose.rotation);
        value.fov = pose.fov;
        if (WriteBoundCameraPose(sequence, shot.cameraBindingId, time, value, parts)) {
            previewChanged_ = true;
            previewEnabled_ = true;
        }
        return;
    }
    shot.camera.owned = true;
    SEQUENCER::ShotPose next = SEQUENCER::EvaluateShotPose(shot.camera, pose.time);
    if (parts & kPosePosition)
        next.position = pose.position;
    if (parts & kPoseRotation)
        next.rotation = pose.rotation;
    if (parts & kPoseFov)
        next.fov = pose.fov;
    SEQUENCER::SetShotPose(shot.camera, next, autoKey_ || !shot.camera.keys.empty());
    previewChanged_ = true;
    previewEnabled_ = true;
}

void CinematicsWorkspaceController::CapturePose(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                const Camera3D& camera) {
    auto* shot = SelectedShot(sequence);
    if (!shot) {
        CreateShot(sequence, camera);
        return;
    }
    WriteShotPose(sequence, *shot, CaptureShotPoseFor(scene, sequence, *shot, camera, ShotLocalTime(*shot)), kPoseAll);
}

bool CinematicsWorkspaceController::AddPoseKeyAtCursor(CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr)
        return false;
    const float local = ShotLocalTime(*shot);
    if (!shot->camera.owned) {
        BoundCameraPose pose;
        const float time = shot->startTimeSeconds + local;
        if (!ReadBoundCameraPose(sequence, shot->cameraBindingId, time, pose))
            return false;
        previewChanged_ = WriteBoundCameraPose(sequence, shot->cameraBindingId, time, pose, BoundAll);
        previewEnabled_ = true;
        return previewChanged_;
    }
    SEQUENCER::SetShotPose(shot->camera, SEQUENCER::EvaluateShotPose(shot->camera, local), true);
    previewChanged_ = true;
    previewEnabled_ = true;
    return true;
}

bool CinematicsWorkspaceController::RemovePoseKeyAtCursor(CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr || !(shot->camera.owned ? SEQUENCER::RemoveShotPoseKey(shot->camera, ShotLocalTime(*shot))
                                                : RemoveBoundCameraKey(sequence, shot->cameraBindingId,
                                                                       shot->startTimeSeconds + ShotLocalTime(*shot))))
        return false;
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::SeekPoseKey(CinematicSequence& sequence, int direction) {
    const auto* shot = SelectedShot(sequence);
    if (!shot)
        return false;
    std::vector<float> times;
    if (shot->camera.owned)
        for (const auto& key : shot->camera.keys)
            times.push_back(shot->startTimeSeconds + key.time);
    else
        times = BoundCameraKeyTimes(sequence, shot->cameraBindingId);
    std::optional<float> found;
    for (float time : times) {
        if (time < shot->startTimeSeconds || time > shot->startTimeSeconds + shot->durationSeconds)
            continue;
        const bool ahead = direction > 0 ? time > cursor_.GetTimeSeconds() + SEQUENCER::kShotPoseKeyEpsilon
                                         : time < cursor_.GetTimeSeconds() - SEQUENCER::kShotPoseKeyEpsilon;
        if (ahead && (!found || (direction > 0) == (time < *found)))
            found = time;
    }
    if (!found)
        return false;
    cursor_.Pause();
    cursor_.Seek(*found);
    previewEnabled_ = true;
    return true;
}

bool CinematicsWorkspaceController::SplitSelectedShot(CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr)
        return false;
    constexpr float kMinimum = 1.0f / 30.0f;
    const float at = cursor_.GetTimeSeconds() - shot->startTimeSeconds;
    if (at < kMinimum || at > shot->durationSeconds - kMinimum)
        return false;
    SEQUENCER::CameraCutClip second = *shot;
    second.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    second.startTimeSeconds = shot->startTimeSeconds + at;
    second.durationSeconds = shot->durationSeconds - at;
    // 後半は前半の最後の画の続き。混ぜると同じ画どうしの混合になるだけなので切り替える。
    second.transition = {};
    second.camera.name = shot->camera.name + " 2";
    shot->durationSeconds = at;
    auto& first = shot->camera;
    auto& rest = second.camera;
    if (first.owned && !first.keys.empty()) {
        const SEQUENCER::ShotPose boundary = SEQUENCER::EvaluateShotPose(first, at);
        std::erase_if(first.keys, [&](const auto& key) { return key.time > at; });
        std::erase_if(rest.keys, [&](const auto& key) { return key.time < at; });
        for (auto& key : rest.keys)
            key.time -= at;
        SEQUENCER::SetShotPose(first, boundary, true);
        SEQUENCER::ShotPose start = boundary;
        start.time = 0.0f;
        SEQUENCER::SetShotPose(rest, start, true);
    }
    SplitCurve(first.railT, rest.railT, at);
    SplitCurve(first.aimWeight, rest.aimWeight, at);
    SplitTimedEffects<SEQUENCER::CameraMotionEffect>(
        first.motion, rest.motion, at, [](const SEQUENCER::CameraMotionEffect& effect) { return effect.duration; });
    SplitTimedEffects<SEQUENCER::SequenceScreenEffect>(
        shot->effects, second.effects, at,
        [](const SEQUENCER::SequenceScreenEffect& effect) { return effect.duration; });
    SEQUENCER::NormalizeShotCamera(first);
    SEQUENCER::NormalizeShotCamera(rest);
    selectedId_ = second.id;
    sequence.cameraCutTrack.clips.push_back(std::move(second));
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::DuplicateSelectedShot(CinematicSequence& sequence) {
    const auto* shot = SelectedShot(sequence);
    if (shot == nullptr)
        return false;
    auto copy = *shot;
    copy.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    copy.camera.name = shot->camera.name + " Copy";
    // 直後が空いていればそこへ、塞がっていれば全ての镜头の後ろへ置く。
    const float end = shot->startTimeSeconds + shot->durationSeconds;
    float latest = 0.0f;
    bool occupied = false;
    for (const auto& other : sequence.cameraCutTrack.clips) {
        latest = (std::max)(latest, other.startTimeSeconds + other.durationSeconds);
        occupied |= other.id != shot->id && other.startTimeSeconds < end + copy.durationSeconds &&
                    other.startTimeSeconds + other.durationSeconds > end;
    }
    copy.startTimeSeconds = occupied ? latest : end;
    selectedId_ = copy.id;
    cursor_.Seek(copy.startTimeSeconds);
    sequence.durationSeconds = (std::max)(sequence.durationSeconds, copy.startTimeSeconds + copy.durationSeconds);
    sequence.cameraCutTrack.clips.push_back(std::move(copy));
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::DeleteSelectedClip(CinematicSequence& sequence) {
    size_t removed = 0;
    const auto selected = [this](const auto& clip) { return clip.id == selectedId_; };
    if (selectionKind_ == SelectionKind::Shot)
        removed = std::erase_if(sequence.cameraCutTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Cue)
        removed = std::erase_if(sequence.presentationCueTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Effect)
        removed = std::erase_if(sequence.screenEffects, selected);
    if (!removed)
        return false;
    selectedId_ = 0;
    curveView_.selectedKey = -1;
    channelOwner_ = 0;
    draggingId_ = 0;
    pilot_ = false;
    selectedRailPoint_ = -1;
    return true;
}

float CinematicsWorkspaceController::SnapTimelineTime(const CinematicSequence& sequence, float seconds,
                                                      uint64_t ignoreShot, bool includeCursor) const {
#if defined(HIKARI_WITH_EDITOR)
    if (ImGui::GetIO().KeyAlt)
        return (std::max)(0.0f, seconds);
    // 8 画素以内の境目へ寄せる。無ければ 1/60 秒の刻み。
    const float reach = 8.0f / (std::max)(zoom_, 1.0f);
    float best = std::round(seconds * 60.0f) / 60.0f;
    float bestDistance = reach;
    const auto consider = [&](float candidate) {
        const float distance = std::abs(candidate - seconds);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    };
    consider(0.0f);
    if (includeCursor)
        consider(cursor_.GetTimeSeconds());
    for (const auto& shot : sequence.cameraCutTrack.clips) {
        if (shot.id == ignoreShot)
            continue;
        consider(shot.startTimeSeconds);
        consider(shot.startTimeSeconds + shot.durationSeconds);
    }
    return (std::max)(0.0f, best);
#else
    (void)sequence;
    (void)ignoreShot;
    (void)includeCursor;
    return seconds;
#endif
}
} // namespace HIKARI::EDITOR
