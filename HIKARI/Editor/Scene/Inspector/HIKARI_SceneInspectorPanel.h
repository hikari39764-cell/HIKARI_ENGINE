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
enum class SceneObjectCommandId;

class SceneInspectorPanel {
public:
    void Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
              SceneObjectCommandService& commands, bool* open = nullptr);

    void DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                      SceneObjectCommandService& commands);

    void DrawObjectContextMenu(DocumentSceneBase& scene, EditorContext& context,
                               const SelectionSyncService& selectionSync, SceneObjectCommandService& commands,
                               GameObject& object);
    void ExecutePendingObjectCommand(DocumentSceneBase& scene, EditorContext& context,
                                     const SelectionSyncService& selectionSync, SceneObjectCommandService& commands);

    void RequestRename(GameObject& object);
    void DrawDeferredDialogs(DocumentSceneBase& scene, EditorContext& context,
                             const SelectionSyncService& selectionSync, SceneObjectCommandService& commands);

    std::optional<SceneObjectAuthoringHistoryRequest> ConsumeHistoryRequest();
    std::optional<SceneObjectId> ConsumeOpenCinematicsWorkspaceCameraRequest();
    void RequestFocusObject(SceneObjectId id) { focusObjectRequest_ = id; }
    std::optional<SceneObjectId> ConsumeFocusObjectRequest();
    IModelMaterialSlotProvider& GetModelMaterialSlotProvider() noexcept {
        return componentAuthoringSection_.GetModelMaterialSlotProvider();
    }

private:
    struct PendingObjectCommand {
        SceneObjectCommandId command;
        std::string argument{};
        const DocumentSceneBase* scene = nullptr;
        uint64_t documentRevision = 0;
        std::vector<SceneObjectId> selection{};
    };

    struct PendingTransformHistory {
        std::vector<SceneObjectData> beforeObjects{};
        SceneCameraSettings beforeCamera{};
        bool dirtyBefore = false;
    };

    void DrawTransform(DocumentSceneBase& scene, EditorContext& context, SceneObjectData& target);
    void DrawAddComponentPopup(DocumentSceneBase& scene, EditorContext& context,
                               const SelectionSyncService& selectionSync, SceneObjectCommandService& commands);
    SceneComponentAuthoringSection componentAuthoringSection_{};
    CollisionAuthoringDialog collisionAuthoringDialog_{};
    CollisionAuthoringService collisionAuthoringService_{};
    std::optional<PendingTransformHistory> pendingTransformHistory_{};
    std::optional<PendingObjectCommand> pendingObjectCommand_{};
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
