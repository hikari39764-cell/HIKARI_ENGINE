#pragma once
#include "Editor/Authoring/HIKARI_SceneObjectAuthoringTypes.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include <optional>

namespace HIKARI {
class DocumentSceneBase;
struct EditorContext;
class SelectionSyncService;
namespace EDITOR {
class SceneObjectCommandService;
// 単一操作の入力と履歴リクエスト。履歴の所有・確定は文書コントローラーに残す。
struct SceneObjectOperationContext {
    DocumentSceneBase& scene;
    EditorContext& editor;
    const SelectionSyncService& selection;
    SceneObjectCommandService& commands;
    std::optional<SceneObjectAuthoringHistoryRequest> history;
};
using SceneObjectOperation = EditorOperationResult (*)(SceneObjectOperationContext&, const nlohmann::json&);
EditorOperationResult SetSceneObjectTransform(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateSceneModel(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateSceneWorldSprite(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateSceneWorldText(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateScenePrimitive(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateSceneEmptyObject(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult CreateSceneWaterObject(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult DropSceneObjectsToGround(SceneObjectOperationContext& operation,
                                               const nlohmann::json& parameters);
EditorOperationResult DeleteSceneObjects(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
EditorOperationResult DuplicateSceneObjects(SceneObjectOperationContext& operation, const nlohmann::json& parameters);
} // namespace EDITOR
} // namespace HIKARI
