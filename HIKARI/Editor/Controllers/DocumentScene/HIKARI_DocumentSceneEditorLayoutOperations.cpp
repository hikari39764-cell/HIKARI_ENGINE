#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Authoring/HIKARI_SceneTransformAuthoring.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {
void DocumentSceneEditorController::RegisterSceneLayoutOperations() {
    using namespace EDITOR;
    operationRegistry_.Register({"scene.layout.query",
                                 "Read local object transforms for a project layout draft.",
                                 "scene",
                                 {},
                                 [this](const nlohmann::json& p) { return ExecuteSceneLayoutOperation(false, p); }},
                                HIKARI::EDITOR::EditorOperationKind::Query);
    operationRegistry_.Register(
        {"scene.layout.apply",
         "Atomically apply a layout draft as one Undo step. Reject stale scene identity or edited targets.",
         "scene",
         {{"sceneId", EditorOperationParameterType::String, true, "Identity from scene.layout.query."},
          {"documentRevision", EditorOperationParameterType::Int, true, "Revision from scene.layout.query."},
          {"commands", EditorOperationParameterType::Json, true,
           "1..256 {objectId, expected, position?, rotation?, scale?}; expected comes from the query."}},
         [this](const nlohmann::json& p) { return ExecuteSceneLayoutOperation(true, p); }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {"scene.undo",
         "Undo the last Scene document edit.",
         "scene",
         {},
         [this](const nlohmann::json&) {
             if (!aiOperationScene_)
                 return EditorOperationResult{false, "NO_SCENE", "Open a scene first."};
             if (aiOperationScene_->IsRuntimePlayActive())
                 return EditorOperationResult{false, "PLAY_ACTIVE", "Stop Play before editing."};
             if (!documentHistory_.CanUndo())
                 return EditorOperationResult{false, "NOTHING_TO_UNDO", "No Scene edit to undo."};
             ExecuteSceneDocumentHistory(*aiOperationScene_, false);
             return EditorOperationResult{true, "OK", "Scene edit undone."};
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    operationRegistry_.Register(
        {"scene.redo",
         "Redo the last undone Scene document edit.",
         "scene",
         {},
         [this](const nlohmann::json&) {
             if (!aiOperationScene_)
                 return EditorOperationResult{false, "NO_SCENE", "Open a scene first."};
             if (aiOperationScene_->IsRuntimePlayActive())
                 return EditorOperationResult{false, "PLAY_ACTIVE", "Stop Play before editing."};
             if (!documentHistory_.CanRedo())
                 return EditorOperationResult{false, "NOTHING_TO_REDO", "No Scene edit to redo."};
             ExecuteSceneDocumentHistory(*aiOperationScene_, true);
             return EditorOperationResult{true, "OK", "Scene edit redone."};
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteSceneLayoutOperation(
    bool apply, const nlohmann::json& parameters) {
    using namespace EDITOR;
    auto* scene = aiOperationScene_;
    if (!scene)
        return {false, "NO_SCENE", "Open a scene first."};
    if (scene->GetSceneDocument().objects.size() > 10000)
        return {false, "SCENE_TOO_LARGE", "Use object queries for scenes above 10,000 objects."};
    const auto identity = scene->GetSceneId() + "|" + scene->GetScenePath();
    if (apply) {
        if (scene->IsRuntimePlayActive())
            return {false, "PLAY_ACTIVE", "Stop Play before applying a layout."};
        if (parameters.value("sceneId", std::string{}) != identity ||
            parameters.value("documentRevision", UINT64_MAX) != scene->GetSceneDocumentRevision())
            return {false, "DOCUMENT_CONFLICT", "The Scene was replaced. Refresh the layout draft."};
        const auto& commands = parameters.at("commands");
        if (!commands.is_array())
            return {false, "INVALID_COMMANDS", "commands must be an array."};
        for (const auto& command : commands) {
            if (!command.is_object() || !command.contains("expected"))
                return {false, "EXPECTED_REQUIRED",
                        "Every layout edit must include the previously observed transform."};
        }
        std::vector<SceneObjectData> staged;
        const auto result = StageSceneTransformEdits(scene->GetSceneDocument(), commands, staged);
        if (!result.ok || result.code == "NO_CHANGE")
            return result;
        const auto before = scene->GetSceneDocument().objects;
        const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
        scene->GetSceneDocument().objects = std::move(staged);
        CommitSceneObjectAuthoringHistory(*scene, {"Arrange Scene Objects", before, scene->GetSceneDocument().objects,
                                                   scene->GetSceneDocument().camera, scene->GetSceneDocument().camera,
                                                   dirty, true, true});
    }
    EditorOperationResult result{true, "OK", apply ? "Layout applied as one Scene edit." : "Layout snapshot ready."};
    result.data = {{"sceneId", identity},
                   {"documentRevision", scene->GetSceneDocumentRevision()},
                   {"objects", nlohmann::json::array()}};
    // Project tools can target exact objects without parsing native scene files.
    for (const auto& object : scene->GetSceneDocument().objects) {
        result.data["objects"].push_back({{"objectId", object.id.value},
                                          {"name", object.name},
                                          {"locked", object.editorLocked},
                                          {"transform", DescribeSceneObjectTransform(object)}});
    }
    return result;
}
} // namespace HIKARI
