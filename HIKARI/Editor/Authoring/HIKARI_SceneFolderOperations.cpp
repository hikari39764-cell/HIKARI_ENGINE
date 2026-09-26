#include "Editor/Authoring/HIKARI_SceneFolderOperations.h"

#include <vector>

namespace HIKARI::EDITOR {
void RegisterSceneFolderOperations(EditorOperationRegistry& registry, SceneFolderExecutor execute) {
    using T = EditorOperationParameterType;
    struct FolderOperation {
        const char* id;
        const char* summary;
        std::vector<EditorOperationParameter> parameters;
        const char* action;
        EditorOperationKind kind;
    };
    const FolderOperation operations[] = {
        {"scene.folder.create",
         "Create an organizational scene folder without changing transforms.",
         {{"path", T::String, true, "フォルダーパス。階層は / で区切ります。"}},
         "create",
         EditorOperationKind::Mutation},
        {"scene.folder.rename",
         "Rename or move a scene folder and its descendant folder memberships.",
         {{"path", T::String, true, "変更するフォルダー"}, {"newPath", T::String, true, "変更後のパス"}},
         "rename",
         EditorOperationKind::Mutation},
        {"scene.folder.remove",
         "Dissolve a scene folder; keep every object and move memberships to its parent folder.",
         {{"path", T::String, true, "解散するフォルダー。オブジェクトは削除しません。"}},
         "remove",
         EditorOperationKind::Mutation},
        {"scene.folder.assign",
         "Move selected stable object IDs into a folder; an empty path removes folder membership.",
         {{"path", T::String, true, "移動先。空文字なら未分類"}, {"objectIds", T::Json, true, "SceneObjectIdの配列"}},
         "assign",
         EditorOperationKind::Mutation},
        {"scene.folder.query",
         "List scene folders and stable object memberships, including empty folders.",
         {},
         "query",
         EditorOperationKind::Query},
    };
    for (const FolderOperation& operation : operations) {
        EditorOperationDefinition definition{
            operation.id, operation.summary, "scene", operation.parameters,
            [execute, action = operation.action](const nlohmann::json& p) { return execute(action, p); }};
        definition.permission = operation.kind == EditorOperationKind::Query
                                    ? EditorOperationPermission::ReadEditorState
                                    : EditorOperationPermission::ModifyEditorState;
        registry.Register(std::move(definition), operation.kind);
    }
}

} // namespace HIKARI::EDITOR
