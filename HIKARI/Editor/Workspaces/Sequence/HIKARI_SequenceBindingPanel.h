#pragma once

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

struct SequenceBindingPanelResult {
  bool sequenceChanged = false;
};

SEQUENCER::SequenceBindingId FindOrCreatePortableCameraBinding(
    const SceneDocument &document, CinematicSequence &sequence,
    SceneObjectId cameraObjectId,
    SEQUENCER::SequenceBindingContext &previewBindings);

class SequenceBindingPanel {
public:
  SequenceBindingPanelResult Draw(const SceneDocument &document,
                                  CinematicSequence &sequence,
                                  const AssetGuid &sequenceAssetGuid,
                                  bool portableAsset, bool editingAllowed);

  SEQUENCER::SequenceBindingContext &GetPreviewBindings() noexcept;
  const SEQUENCER::SequenceBindingContext &GetPreviewBindings() const noexcept;
  void Reset();

private:
  void PreparePreviewBindings(const SceneDocument &document,
                              const CinematicSequence &sequence,
                              const AssetGuid &sequenceAssetGuid,
                              bool portableAsset);

  SEQUENCER::SequenceBindingContext previewBindings_{};
  AssetGuid previewSourceAssetGuid_{};
  CinematicSequenceId previewSourceSequenceId_{};
  bool previewSourcePortable_ = false;
  bool previewSourceInitialized_ = false;
};

} // namespace HIKARI::EDITOR
