#pragma once

#include <optional>

#include "Editor/Authoring/HIKARI_CollisionAuthoringDialog.h"
#include "Editor/Authoring/HIKARI_CollisionAuthoringService.h"
#include "Editor/Authoring/HIKARI_SceneComponentAuthoringSection.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {

struct EditorContext;
class DocumentSceneBase;
class GameObject;
class SelectionSyncService;

namespace EDITOR {
class SceneObjectCommandService;

class SceneInspectorPanel {
public:
  void Draw(DocumentSceneBase &scene, EditorContext &context,
            const SelectionSyncService &selectionSync,
            SceneObjectCommandService &commands, bool *open = nullptr);

  void DrawContents(DocumentSceneBase &scene, EditorContext &context,
                    const SelectionSyncService &selectionSync,
                    SceneObjectCommandService &commands);

  void DrawObjectContextMenu(DocumentSceneBase &scene, EditorContext &context,
                             const SelectionSyncService &selectionSync,
                             SceneObjectCommandService &commands,
                             GameObject &object);

  void RequestRename(GameObject &object);
  void RequestFocus(SceneObjectId objectId);
  void DrawDeferredDialogs(DocumentSceneBase &scene, EditorContext &context,
                           const SelectionSyncService &selectionSync,
                           SceneObjectCommandService &commands);

  std::optional<SceneObjectAuthoringHistoryRequest> ConsumeHistoryRequest();
  std::optional<SceneObjectId> ConsumeOpenCinematicsWorkspaceCameraRequest();
  std::optional<SceneObjectId> ConsumeFocusObjectRequest();

private:
  struct PendingTransformHistory {
    std::vector<SceneObjectData> beforeObjects{};
    SceneCameraSettings beforeCamera{};
    bool dirtyBefore = false;
  };

  void DrawTransform(DocumentSceneBase &scene, EditorContext &context,
                     SceneObjectData &target);
  void DrawAddComponentPopup(DocumentSceneBase &scene, EditorContext &context,
                             const SelectionSyncService &selectionSync,
                             SceneObjectCommandService &commands);
  SceneComponentAuthoringSection componentAuthoringSection_{};
  CollisionAuthoringDialog collisionAuthoringDialog_{};
  CollisionAuthoringService collisionAuthoringService_{};
  std::optional<PendingTransformHistory> pendingTransformHistory_{};
  std::optional<SceneObjectAuthoringHistoryRequest> historyRequest_{};
  std::optional<SceneObjectId> openCinematicsWorkspaceCameraRequest_{};
  std::optional<SceneObjectId> focusObjectRequest_{};
  SceneObjectId renameObjectId_{};
  SceneObjectId prefabObjectId_{};
  bool openRenamePopup_ = false;
  bool openPrefabPopup_ = false;
  bool openComponentPicker_ = false;
  char renameBuffer_[128]{};
  char prefabBuffer_[128]{};
  char componentSearchBuffer_[96]{};
};

} // namespace EDITOR
} // namespace HIKARI
