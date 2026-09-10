#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Authoring/HIKARI_SceneFolderAuthoring.h"
#include "Editor/Authoring/HIKARI_SceneFolderOperations.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {
    void DocumentSceneEditorController::RegisterSceneFolderOperations() {
        EDITOR::RegisterSceneFolderOperations(operationRegistry_,
            [this](std::string_view operation, const nlohmann::json& parameters) {
                return ExecuteSceneFolderOperation(operation, parameters);
            });
    }

    EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteSceneFolderOperation(
        std::string_view operation, const nlohmann::json& parameters) {
        auto* scene = aiOperationScene_;
        if (!scene) return {false, "NO_SCENE", "場面を開いてから操作してください。"};
        const bool dirtyBefore = context_.sceneDirty || scene->HasUnsavedSceneChanges();
        auto edit = EDITOR::ApplySceneFolderOperation(scene->GetSceneDocument(), operation, parameters);
        if (edit.command) CommitAppliedDocumentCommand(*scene, std::move(edit.command), 0u, dirtyBefore, EDITOR::EditorDocumentImpact::None);
        if (edit.result.ok) {
            edit.result.data["sceneName"] = scene->GetSceneDocument().sceneName;
            edit.result.data["documentRevision"] = scene->GetSceneDocumentRevision();
            if (operation != "query") {
                EDITOR::EditorWorkspaceOpenRequest request{};
                request.workspaceId = EDITOR::EditorWorkspaceId::Scene;
                (void)workspaceHost_.RequestOpen(std::move(request));
            }
        }
        return std::move(edit.result);
    }
}
