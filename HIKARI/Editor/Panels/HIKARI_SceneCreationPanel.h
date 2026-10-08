#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "Editor/Authoring/HIKARI_PrimitiveCreationDialog.h"
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"

namespace HIKARI {

struct EditorContext;
class DocumentSceneBase;
class GameObject;
class SelectionSyncService;

namespace EDITOR {
class SceneObjectCommandService;

class SceneCreationPanel {
public:
    void DrawCreationMenu(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                          SceneObjectCommandService& commands);

    void DrawDeferredDialogs(DocumentSceneBase& scene, EditorContext& context,
                             const SelectionSyncService& selectionSync);

    std::optional<SceneObjectAuthoringHistoryRequest> ConsumeHistoryRequest();

private:
    static bool MatchesSearch(std::string_view label, std::string_view search);
    void DrawUiCreationMenu(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                            std::string_view search);
    void CreateUiObject(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                        bool button, bool addSceneFlow = false);
    // create が文書へ物体を加えたら選択し、取り消しの履歴を残す。
    void CreateWithHistory(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                           std::string historyLabel, const std::function<GameObject*()>& create);

    PrimitiveCreationDialog primitiveCreationDialog_{};
    std::optional<SceneObjectAuthoringHistoryRequest> historyRequest_{};
    char searchBuffer_[96]{};
};

} // namespace EDITOR
} // namespace HIKARI
