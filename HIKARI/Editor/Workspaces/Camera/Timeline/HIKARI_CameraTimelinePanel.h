#pragma once

#include <array>
#include <cstdint>

#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineCanvas.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceBindingPanel.h"
#include "Scene/HIKARI_CinematicSequencePlayer.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

struct CameraTimelinePanelResult {
  bool documentChanged = false;
  bool previewEnabled = false;
  bool forceCameraCut = false;
  uint64_t editMergeId = 0;
  CinematicSequenceId sequenceId{};
  CinematicCameraEvaluation evaluation{};
};

class CameraTimelinePanel {
public:
  CameraTimelinePanelResult
  Draw(SceneDocument &document, SceneCinematicsSettings &settings,
       const AssetGuid &sequenceAssetGuid, SceneObjectId selectedCameraObjectId,
       float deltaTime, bool previewAllowed, bool collectionEditingAllowed);

  void ResetForScene();
  void PausePlayback();
  void SuspendPreview();
  CinematicSequence *GetActiveSequence(SceneCinematicsSettings &settings);

private:
  bool EnsureActiveSequence(SceneCinematicsSettings &settings);
  bool SetActiveSequence(SceneCinematicsSettings &settings,
                         CinematicSequenceId sequenceId);
  void SyncNameBuffer(const CinematicSequence &sequence);

  CinematicSequencePlayer player_{};
  SequenceBindingPanel bindingPanel_{};
  CameraTimelineCanvas canvas_{};
  CinematicSequenceId activeSequenceId_{};
  std::array<char, 128> sequenceNameBuffer_{};
  bool previewEnabled_ = true;
  bool previewCutPending_ = true;
};

} // namespace HIKARI::EDITOR
