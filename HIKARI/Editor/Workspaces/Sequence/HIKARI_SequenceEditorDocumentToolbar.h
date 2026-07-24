#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

class AssetDatabase;
struct CinematicSequence;

namespace EDITOR {

class SequenceEditorDocumentController;

struct SequenceEditorDocumentToolbarResult {
  bool toggleLibraryRequested = false;
  bool saveEmbeddedSceneRequested = false;
  bool revealAssetRequested = false;
  AssetGuid revealAssetGuid{};
  std::filesystem::path revealAssetPath{};
  std::string statusMessage{};
};

class SequenceEditorDocumentToolbar {
public:
  SequenceEditorDocumentToolbarResult
  Draw(SequenceEditorDocumentController &controller,
       AssetDatabase &assetDatabase, const CinematicSequence *activeSequence,
       const std::string &sceneDisplayName, bool libraryVisible,
       bool actionsAllowed) const;
  void DrawPendingConfirmation(SequenceEditorDocumentController &controller,
                               AssetDatabase &assetDatabase,
                               std::string &inOutStatusMessage) const;
};

} // namespace EDITOR
} // namespace HIKARI
