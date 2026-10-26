#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
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
float CinematicsWorkspaceController::ShotLocalTime(const SEQUENCER::CameraCutClip& shot) const {
    return std::clamp(cursor_.GetTimeSeconds() - shot.startTimeSeconds, 0.0f, shot.durationSeconds);
}

float CinematicsWorkspaceController::ShotSourceTime(const SEQUENCER::CameraCutClip& shot) const {
    return SEQUENCER::ShotMaterialTime(shot.camera, ShotLocalTime(shot) + shot.sourceOffsetSeconds);
}
bool CinematicsWorkspaceController::SharedPoseAtCursor(const SEQUENCER::CameraCutClip& shot) const {
    const auto* key = SEQUENCER::FindShotPoseKey(shot.camera, ShotSourceTime(shot));
    return key ? key->reference.IsValid() : shot.camera.keys.empty() && shot.camera.base.reference.IsValid();
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
    shot.camera.useScenePose = false;
    shot.camera.name = "Shot " + std::to_string(sequence.cameraCutTrack.clips.size() + 1);
    SEQUENCER::SetShotPose(shot.camera, SEQUENCER::CaptureShotPose(camera), false);
    shot.camera.base.nearClip = camera.GetNearZ();
    shot.camera.base.farClip = camera.GetFarZ();
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
    return SEQUENCER::CaptureShotPose(camera,
                                      SEQUENCER::ShotMaterialTime(shot.camera, keyTime + shot.sourceOffsetSeconds),
                                      anchor ? &anchorMatrix : nullptr);
}

void CinematicsWorkspaceController::WriteShotPose(CinematicSequence& sequence, SEQUENCER::CameraCutClip& shot,
                                                  const SEQUENCER::ShotPose& pose, uint32_t parts) {
    (void)sequence;
    if (SharedPoseAtCursor(shot))
        return;
    shot.camera.useScenePose = false;
    SEQUENCER::ShotPose next = SEQUENCER::EvaluateShotPose(shot.camera, pose.time);
    // 新しい途中キーは解決済みの姿勢を保存し、隣の端点の参照を継承しない。
    if (!SEQUENCER::FindShotPoseKey(shot.camera, pose.time))
        next = pose;
    next.time = pose.time;
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

bool CinematicsWorkspaceController::AddPoseKeyAtCursor(DocumentSceneBase& scene, CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr)
        return false;
    if (SharedPoseAtCursor(*shot))
        return false;
    SEQUENCER::ShotPose pose;
    if (!SEQUENCER::EvaluateSequenceShotPose(sequence, previewBindings_, *shot, scene.GetWorld(),
                                             scene.GetCameraDirector(), ShotSourceTime(*shot), pose))
        return false;
    SEQUENCER::SetShotPose(shot->camera, pose, true);
    previewChanged_ = true;
    previewEnabled_ = true;
    return true;
}

bool CinematicsWorkspaceController::RemovePoseKeyAtCursor(CinematicSequence& sequence) {
    auto* shot = SelectedShot(sequence);
    if (shot == nullptr || SharedPoseAtCursor(*shot) ||
        !SEQUENCER::RemoveShotPoseKey(shot->camera, ShotSourceTime(*shot)))
        return false;
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::SeekPoseKey(CinematicSequence& sequence, int direction) {
    const auto* shot = SelectedShot(sequence);
    if (!shot || !shot->camera.timing.keys.empty())
        return false;
    std::vector<float> times;
    for (const auto& key : shot->camera.keys)
        times.push_back(shot->startTimeSeconds + key.time - shot->sourceOffsetSeconds);
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
    const auto id = SplitSequenceShot(sequence, selectedId_, cursor_.GetTimeSeconds());
    if (!id)
        return false;
    selectedId_ = id;
    previewChanged_ = true;
    return true;
}
bool CinematicsWorkspaceController::DuplicateSelectedShot(CinematicSequence& sequence) {
    const auto id = DuplicateSequenceShot(sequence, selectedId_);
    if (!id)
        return false;
    selectedId_ = id;
    cursor_.Seek(SelectedShot(sequence)->startTimeSeconds);
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::DeleteSelectedClip(CinematicSequence& sequence) {
    size_t removed = 0;
    const auto selected = [this](const auto& clip) { return clip.id == selectedId_; };
    // 物体轨は键を選んでいればその键だけ、轨だけを選んでいれば轨ごと消す。
    if (auto* track = SelectedObjectTrack(sequence);
        track && selectedObjectKey_ >= 0 && selectedObjectKey_ < static_cast<int>(track->keys.size())) {
        track->keys.erase(track->keys.begin() + selectedObjectKey_);
        selectedObjectKey_ = -1;
        return true;
    }
    if (selectionKind_ == SelectionKind::Object)
        removed = std::erase_if(sequence.objectTracks, selected);
    if (selectionKind_ == SelectionKind::Shot)
        removed = std::erase_if(sequence.cameraCutTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Cue)
        removed = std::erase_if(sequence.presentationCueTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Effect)
        removed = std::erase_if(sequence.screenEffects, selected);
    else if (selectionKind_ == SelectionKind::Event)
        removed = std::erase_if(sequence.eventTrack.clips, selected);
    else if (selectionKind_ == SelectionKind::Text)
        removed = std::erase_if(sequence.textTrack.clips, selected);
    if (!removed)
        return false;
    selectedId_ = 0;
    curveView_.editing.selection.clear();
    channelOwner_ = 0;
    draggingId_ = 0;
    pilot_ = false;
    selectedRailPoint_ = -1;
    return true;
}

float CinematicsWorkspaceController::SnapTimelineTime(const CinematicSequence& sequence, float seconds,
                                                      uint64_t ignoreShot, bool includeCursor) const {
#if defined(HIKARI_WITH_EDITOR)
    if (ImGui::GetIO().KeyAlt || !curveView_.snap)
        return (std::max)(0.0f, seconds);
    // 8 画素以内の境目へ寄せる。無ければ編集 fps の刻み。
    const float reach = 8.0f / (std::max)(zoom_, 1.0f);
    float best = std::round(seconds * curveView_.fps) / curveView_.fps;
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
