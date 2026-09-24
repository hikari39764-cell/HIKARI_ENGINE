#include "Editor/Authoring/HIKARI_SceneFolderOperations.h"

namespace HIKARI::EDITOR {
void RegisterSceneFolderOperations(EditorOperationRegistry& registry, SceneFolderExecutor execute) {
    using namespace EDITOR;
    using T = EditorOperationParameterType;
    registry.Register({"scene.folder.create",
                       "Create an organizational scene folder without changing transforms.",
                       "scene",
                       {{"path", T::String, true, "フォルダーパス。階層は / で区切ります。"}},
                       [execute](const nlohmann::json& p) { return execute("create", p); },
                       EditorOperationExecutionPolicy::MainThread,
                       EditorOperationKind::Mutation,
                       EditorOperationExposureMask::LegacyDefault,
                       EditorOperationPermission::ModifyEditorState,
                       EditorOperationRisk::Low},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"scene.folder.rename",
                       "Rename or move a scene folder and its descendant folder memberships.",
                       "scene",
                       {{"path", T::String, true, "変更するフォルダー"}, {"newPath", T::String, true, "変更後のパス"}},
                       [execute](const nlohmann::json& p) { return execute("rename", p); },
                       EditorOperationExecutionPolicy::MainThread,
                       EditorOperationKind::Mutation,
                       EditorOperationExposureMask::LegacyDefault,
                       EditorOperationPermission::ModifyEditorState,
                       EditorOperationRisk::Low},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"scene.folder.remove",
                       "Dissolve a scene folder; keep every object and move memberships to its parent folder.",
                       "scene",
                       {{"path", T::String, true, "解散するフォルダー。オブジェクトは削除しません。"}},
                       [execute](const nlohmann::json& p) { return execute("remove", p); },
                       EditorOperationExecutionPolicy::MainThread,
                       EditorOperationKind::Mutation,
                       EditorOperationExposureMask::LegacyDefault,
                       EditorOperationPermission::ModifyEditorState,
                       EditorOperationRisk::Low},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {"scene.folder.assign",
         "Move selected stable object IDs into a folder; an empty path removes folder membership.",
         "scene",
         {{"path", T::String, true, "移動先。空文字なら未分類"}, {"objectIds", T::Json, true, "SceneObjectIdの配列"}},
         [execute](const nlohmann::json& p) { return execute("assign", p); },
         EditorOperationExecutionPolicy::MainThread,
         EditorOperationKind::Mutation,
         EditorOperationExposureMask::LegacyDefault,
         EditorOperationPermission::ModifyEditorState,
         EditorOperationRisk::Low},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"scene.folder.query",
                       "List scene folders and stable object memberships, including empty folders.",
                       "scene",
                       {},
                       [execute](const nlohmann::json& p) { return execute("query", p); },
                       EditorOperationExecutionPolicy::MainThread,
                       EditorOperationKind::Query,
                       EditorOperationExposureMask::LegacyDefault,
                       EditorOperationPermission::ReadEditorState,
                       EditorOperationRisk::Low},
                      HIKARI::EDITOR::EditorOperationKind::Query);
}

} // namespace HIKARI::EDITOR
