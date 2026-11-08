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

    // create が文書へ物体を加えたら選択し、取り消しの履歴を残す（布景の配置も同じ経路を使う）。
    void CreateWithHistory(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync,
                           std::string historyLabel, const std::function<GameObject*()>& create);

private:
    static bool MatchesSearch(std::string_view label, std::string_view search);

    PrimitiveCreationDialog primitiveCreationDialog_{};
    std::optional<SceneObjectAuthoringHistoryRequest> historyRequest_{};
    char searchBuffer_[96]{};
};

} // namespace EDITOR
} // namespace HIKARI
