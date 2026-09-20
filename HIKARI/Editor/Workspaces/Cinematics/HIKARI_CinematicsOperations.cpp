#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsOperations.h"

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {
namespace {

EditorOperationParameter MakeOperationParameter(
    std::string name,
    EditorOperationParameterType type,
    bool required,
    std::string description) {
  EditorOperationParameter parameter{};
  parameter.name = std::move(name);
  parameter.type = type;
  parameter.required = required;
  parameter.description = std::move(description);
  return parameter;
}

bool RequireSequenceAssetDocument(
    const CinematicsWorkspaceController& controller,
    EditorOperationResult& result) {
  if (controller.IsEditingSequenceAsset()) {
    return true;
  }
  result.code = "NO_SEQUENCE_ASSET";
  result.message =
      "当前没有打开序列资产文档（Scene Cinematics 属于场景文档，"
      "请使用 scene.save / 场景撤销）。";
  result.hints = {
      "先用 cinematics.list_assets 查询序列资产，"
      "再用 cinematics.open_asset 打开。",
      "场景内嵌时间线请使用 scene.save / scene.undo_last。",
  };
  return false;
}

bool RequireScene(
    const std::function<DocumentSceneBase*()>& sceneProvider,
    EditorOperationResult& result,
    DocumentSceneBase*& outScene) {
  outScene = sceneProvider();
  if (outScene == nullptr) {
    result.code = "NO_SCENE";
    result.message = "当前没有可用的场景上下文。";
    return false;
  }
  return true;
}

const char* SourceKindName(SequenceEditorSourceKind kind) {
  switch (kind) {
  case SequenceEditorSourceKind::EmbeddedScene:
    return "EmbeddedScene";
  case SequenceEditorSourceKind::TransientAsset:
    return "TransientAsset";
  case SequenceEditorSourceKind::Asset:
    return "Asset";
  }
  return "Unknown";
}

void AppendSequenceSummary(
    const CinematicSequence& sequence,
    EditorOperationResult& result) {
  std::string line =
      "sequence: " + sequence.name +
      " (id " + std::to_string(sequence.id.value) +
      ", duration " + std::to_string(sequence.durationSeconds) + "s, shots " +
      std::to_string(sequence.cameraCutTrack.clips.size()) + ")";
  result.details.push_back(std::move(line));
}

void AppendDocumentState(
    const CinematicsWorkspaceController& controller,
    EditorOperationResult& result) {
  const SequenceEditorDocument& document =
      controller.GetSequenceDocument();
  result.details.push_back(
      std::string("sourceKind: ") + SourceKindName(document.GetSourceKind()));
  result.details.push_back("document: " + document.GetDisplayName());
  result.details.push_back(
      std::string("dirty: ") +
      (document.IsDirty() ? "true" : "false"));
  result.details.push_back(
      std::string("canUndo: ") +
      (document.CanUndo() ? "true" : "false"));
  result.details.push_back(
      std::string("canRedo: ") +
      (document.CanRedo() ? "true" : "false"));
  if (document.GetAssetGuid().IsValid()) {
    result.details.push_back(
        "assetGuid: " + document.GetAssetGuid().value);
  }
}

} // namespace

void RegisterCinematicsOperations(
    EditorOperationRegistry& registry,
    CinematicsWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider) {

  registry.Register({
      "cinematics.document", "Read the editable sequence document including shot cameras, curves, motion and screen effects.",
      "cinematics", {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result;
        auto* scene = sceneProvider();
        if (!scene) { result.code="NO_SCENE"; return result; }
        result.ok=true; result.code="OK"; result.message="Sequence document";
        result.details.push_back(controller.DescribeAuthoring(*scene).dump());
        return result;
      },
  });
  registry.Register({
      "cinematics.edit", "Edit the open Sequence asset atomically. command.action: append_shot, edit_shot (id), remove_shot (id), set_screen_effects, rename, replace_sequence. command.value uses the document schema returned by cinematics.document. Curve keys are [seconds, value, inSlope, outSlope] arrays. Edits support undo and never switch the user's workspace.",
      "cinematics", {MakeOperationParameter("command",EditorOperationParameterType::Json,true,"Authoring command object")},
      [&controller, sceneProvider](const nlohmann::json& p) {
        EditorOperationResult result; auto* scene=sceneProvider();
        if (!scene) { result.code="NO_SCENE"; return result; }
        result.ok=controller.ExecuteAuthoring(*scene,p.at("command"),result.message);
        result.code=result.ok?"OK":"EDIT_REJECTED"; return result;
      },
  });

  registry.Register({
      "cinematics.query",
      "Return the current sequence document state and sequence summary.",
      "cinematics",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        result.ok = true;
        result.code = "OK";
        result.message = "序列文档状态已返回。";
        AppendDocumentState(controller, result);
        DocumentSceneBase* scene = sceneProvider();
        if (scene == nullptr) {
          return result;
        }
        const SequenceEditorDocument& document =
            controller.GetSequenceDocument();
        if (const CinematicSequence* sequence =
                document.GetAssetSequence()) {
          AppendSequenceSummary(*sequence, result);
          return result;
        }
        const SceneCinematicsSettings& settings =
            scene->GetSceneDocument().cinematics;
        for (const CinematicSequence& sequence : settings.sequences) {
          AppendSequenceSummary(sequence, result);
        }
        return result;
      },
  });

  registry.Register({
      "cinematics.save",
      "Save the currently open Sequence asset.",
      "cinematics",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireSequenceAssetDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.SaveSequenceDocument(*scene, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "SAVE_FAILED";
          result.message = message;
        }
        return result;
      },
  });

  registry.Register({
      "cinematics.undo",
      "Undo the most recent Sequence asset edit.",
      "cinematics",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireSequenceAssetDocument(controller, result)) {
          return result;
        }
        std::string message;
        if (controller.UndoSequenceDocument(message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "UNDO_FAILED";
          result.message = message;
        }
        return result;
      },
  });

  registry.Register({
      "cinematics.redo",
      "Redo the most recently undone Sequence asset edit.",
      "cinematics",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireSequenceAssetDocument(controller, result)) {
          return result;
        }
        std::string message;
        if (controller.RedoSequenceDocument(message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "REDO_FAILED";
          result.message = message;
        }
        return result;
      },
  });

  registry.Register({
      "cinematics.list_assets",
      "List Sequence assets in the project (guid, name, source path).",
      "cinematics",
      {},
      [sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        const std::vector<const AssetRecord*> records =
            scene->GetAssetDatabase().CollectByType(AssetType::Sequence);
        if (records.empty()) {
          result.code = "NO_SEQUENCE_ASSETS";
          result.message = "项目中还没有 Sequence 资产。";
          return result;
        }
        for (const AssetRecord* record : records) {
          result.details.push_back(
              "sequence: " + GetAssetRecordDisplayName(*record) +
              " (guid " + record->guid.value + ", " +
              record->sourcePath.generic_string() + ")");
        }
        result.ok = true;
        result.code = "OK";
        result.message =
            "已返回 " + std::to_string(records.size()) +
            " 个 Sequence 资产。";
        return result;
      },
  });

  registry.Register({
      "cinematics.new_asset",
      "Create a new unsaved Sequence asset.",
      "cinematics",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.RequestNewSequenceAsset(*scene, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingSequenceConfirmation()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前序列资产有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 cinematics.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "NEW_ASSET_FAILED";
        result.message = message;
        return result;
      },
  });

  registry.Register({
      "cinematics.open_asset",
      "Open a Sequence asset by its guid.",
      "cinematics",
      {
          MakeOperationParameter(
              "assetGuid", EditorOperationParameterType::String,
              true, "cinematics.list_assets 返回的 guid"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        const std::string guidText =
            parameters.value("assetGuid", std::string{});
        const AssetGuid guid{ guidText };
        if (!guid.IsValid()) {
          result.code = "MISSING_FIELD";
          result.message = "缺少 assetGuid 字段。";
          result.hints = {
              "先用 cinematics.list_assets 查询可用 guid。",
          };
          return result;
        }
        const AssetRecord* record =
            scene->GetAssetDatabase().FindByGuid(guid);
        if (record == nullptr || record->type != AssetType::Sequence) {
          result.code = "ASSET_NOT_FOUND";
          result.message = "找不到该 Sequence 资产：" + guidText;
          result.hints = {
              "先用 cinematics.list_assets 查询可用 guid。",
          };
          return result;
        }
        std::string message;
        if (controller.RequestOpenSequenceAsset(*scene, guid, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingSequenceConfirmation()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前序列资产有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 cinematics.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "OPEN_FAILED";
        result.message = message;
        return result;
      },
  });

  registry.Register({
      "cinematics.revert",
      "Revert the open Sequence asset to its saved version.",
      "cinematics",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireSequenceAssetDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.RequestRevertSequenceAsset(*scene, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingSequenceConfirmation()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前序列资产有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 cinematics.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "REVERT_FAILED";
        result.message = message;
        return result;
      },
  });

  registry.Register({
      "cinematics.close_to_embedded",
      "Close the external Sequence asset and return to Scene Cinematics.",
      "cinematics",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.RequestCloseSequenceAsset(*scene, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingSequenceConfirmation()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前序列资产有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 cinematics.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "CLOSE_FAILED";
        result.message = message;
        return result;
      },
  });
}

} // namespace HIKARI::EDITOR
