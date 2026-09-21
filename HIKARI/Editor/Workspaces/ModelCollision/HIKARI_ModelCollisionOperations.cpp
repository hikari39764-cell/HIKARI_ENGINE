#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionOperations.h"

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {



bool RequireCollisionDocument(
    const ModelCollisionWorkspaceController& controller,
    EditorOperationResult& result) {
  if (controller.IsEditingModel()) {
    return true;
  }
  result.code = "NO_COLLISION_DOCUMENT";
  result.message = "碰撞工作区未打开模型";
  return false;
}



bool RequireSelectedShapes(
    const ModelCollisionWorkspaceController& controller,
    EditorOperationResult& result,
    const char* actionName) {
  if (controller.GetSelectedShapeCount() != 0u) {
    return true;
  }
  result.code = "NO_SELECTED_SHAPES";
  result.message = std::string("没有选中碰撞形状，无法") + actionName;
  return false;
}

bool ParseShapeType(
    const nlohmann::json& parameters,
    ASSETS::COLLISION::CollisionGeometryShapeType& outType,
    EditorOperationResult& result) {
  const std::string type =
      parameters.value("type", std::string{});
  if (type == "box") {
    outType = ASSETS::COLLISION::CollisionGeometryShapeType::Box;
  } else if (type == "sphere") {
    outType = ASSETS::COLLISION::CollisionGeometryShapeType::Sphere;
  } else if (type == "capsule") {
    outType = ASSETS::COLLISION::CollisionGeometryShapeType::Capsule;
  } else if (type == "convex_hull" || type == "convexhull") {
    outType = ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull;
  } else if (type == "triangle_mesh" || type == "trimesh") {
    outType = ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh;
  } else {
    result.code = "UNKNOWN_SHAPE_TYPE";
    result.message = "未知的碰撞形状类型：" + type;
    result.details = {
        "supported: box / sphere / capsule / convex_hull / triangle_mesh",
    };
    return false;
  }
  return true;
}

bool ParseBoolParameter(
    const nlohmann::json& parameters,
    const char* key,
    bool& outValue,
    EditorOperationResult& result) {
  const auto found = parameters.find(key);
  if (found == parameters.end() || !found->is_boolean()) {
    result.code = "MISSING_FIELD";
    result.message = std::string("缺少 ") + key + " 字段（true/false）";
    return false;
  }
  outValue = found->get<bool>();
  return true;
}

} // namespace

void RegisterModelCollisionOperations(
    EditorOperationRegistry& registry,
    ModelCollisionWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider) {

  using ASSETS::COLLISION::CollisionGeometryShapeType;

  registry.Register({
      "collision.open_model",
      "Open a model in the Model Collision workspace in the background.",
      "model_collision",
      {
          EditorOperationParameter{
              "modelGuid", EditorOperationParameterType::String,
              true, "模型资产 guid（asset.list_models 可查）"},
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        const std::string guidText =
            parameters.value("modelGuid", std::string{});
        const AssetGuid guid{ guidText };
        if (!guid.IsValid()) {
          result.code = "MISSING_FIELD";
          result.message = "缺少 modelGuid 字段。";
          return result;
        }
        const AssetRecord* record =
            scene->GetAssetDatabase().FindByGuid(guid);
        if (record == nullptr || record->type != AssetType::Model) {
          result.code = "ASSET_NOT_FOUND";
          result.message = "找不到该模型资产：" + guidText;
          result.hints = {
              "先用 asset.list_models 查询可用模型 guid。",
          };
          return result;
        }
        if (controller.IsDocumentDirty()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前碰撞设置未保存，打开新模型会丢弃未保存修改；"
              "请先调用 collision.save 保存后再打开。";
          result.hints = {
              "先调用 collision.save 保存当前碰撞设置。",
          };
          return result;
        }
        std::string message;
        if (controller.OpenModel(
                scene->GetAssetDatabase(), guid, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "OPEN_FAILED";
          result.message = message;
        }
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.save",
      "Save the current model collision setup and rebuild its runtime artifact.",
      "model_collision",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.SaveDocument(*scene, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "SAVE_FAILED";
          result.message = message;
        }
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.undo",
      "Undo the most recent model collision edit.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        std::string message;
        if (controller.Undo(message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "UNDO_FAILED";
          result.message = message;
        }
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.redo",
      "Redo the most recently undone model collision edit.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        std::string message;
        if (controller.Redo(message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
        } else {
          result.code = "REDO_FAILED";
          result.message = message;
        }
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.add_shape",
      "Add a fitted collision shape to the current model.",
      "model_collision",
      {
          EditorOperationParameter{
              "type", EditorOperationParameterType::String,
              true,
              "box/sphere/capsule/convex_hull/triangle_mesh"},
      },
      [&controller](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        CollisionGeometryShapeType type =
            CollisionGeometryShapeType::Box;
        if (!ParseShapeType(parameters, type, result)) {
          return result;
        }
        controller.AddShape(type);
        result.ok = true;
        result.code = "OK";
        result.message = "已添加碰撞形状：" +
            parameters.value("type", std::string{});
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.delete_selected",
      "Delete the selected collision shapes.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        if (!RequireSelectedShapes(
                controller, result, "删除")) {
          return result;
        }
        controller.DeleteSelectedShapes();
        const std::string& status = controller.GetStatusMessage();
        if (status.find("unlock") != std::string::npos) {
          result.code = "SHAPES_LOCKED";
          result.message = status;
          return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message = "已删除选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.duplicate_selected",
      "Duplicate the selected collision shapes.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        if (!RequireSelectedShapes(
                controller, result, "复制")) {
          return result;
        }
        controller.DuplicateSelectedShapes();
        result.ok = true;
        result.code = "OK";
        result.message = "已复制选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.set_selected_enabled",
      "Enable or disable the selected collision shapes.",
      "model_collision",
      {
          EditorOperationParameter{
              "enabled", EditorOperationParameterType::Bool,
              true, "true 启用，false 禁用"},
      },
      [&controller](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        bool enabled = false;
        if (!ParseBoolParameter(
                parameters, "enabled", enabled, result)) {
          return result;
        }
        controller.SetSelectedShapesEnabled(enabled);
        result.ok = true;
        result.code = "OK";
        result.message = enabled
            ? "已启用选中的碰撞形状"
            : "已禁用选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.set_selected_hidden",
      "Hide or reveal the selected collision shapes in the preview.",
      "model_collision",
      {
          EditorOperationParameter{
              "hidden", EditorOperationParameterType::Bool,
              true, "true 隐藏，false 显示"},
      },
      [&controller](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        bool hidden = false;
        if (!ParseBoolParameter(
                parameters, "hidden", hidden, result)) {
          return result;
        }
        controller.SetSelectedShapesHidden(hidden);
        result.ok = true;
        result.code = "OK";
        result.message = hidden
            ? "已隐藏选中的碰撞形状"
            : "已显示选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.set_selected_locked",
      "Lock or unlock the selected collision shapes.",
      "model_collision",
      {
          EditorOperationParameter{
              "locked", EditorOperationParameterType::Bool,
              true, "true 锁定，false 解锁"},
      },
      [&controller](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        bool locked = false;
        if (!ParseBoolParameter(
                parameters, "locked", locked, result)) {
          return result;
        }
        controller.SetSelectedShapesLocked(locked);
        result.ok = true;
        result.code = "OK";
        result.message = locked
            ? "已锁定选中的碰撞形状"
            : "已解锁选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.show_all",
      "Show every collision shape in the preview.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        controller.ShowAllShapes();
        result.ok = true;
        result.code = "OK";
        result.message = "已显示全部碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.hide_unselected",
      "Hide every collision shape that is not selected.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        controller.HideUnselectedShapes();
        result.ok = true;
        result.code = "OK";
        result.message = "已隐藏未选中的碰撞形状";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.generate",
      "Start background generation using the current generation settings.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        controller.GenerateShapes();
        const std::string& status = controller.GetStatusMessage();
        if (status.find("unavailable") != std::string::npos) {
          result.code = "GENERATION_UNAVAILABLE";
          result.message = status;
          return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message = "已开始后台碰撞生成";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.apply_generation_draft",
      "Apply the pending generation draft to the collision setup.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        if (!controller.HasGenerationDraft()) {
          result.code = "NO_GENERATION_DRAFT";
          result.message = "没有待应用的生成草稿";
          return result;
        }
        controller.ApplyGenerationDraft();
        result.ok = true;
        result.code = "OK";
        result.message = controller.GetStatusMessage().empty()
            ? "已应用生成草稿"
            : controller.GetStatusMessage();
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);

  registry.Register({
      "collision.discard_generation_draft",
      "Discard the pending generation draft without applying it.",
      "model_collision",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireCollisionDocument(controller, result)) {
          return result;
        }
        controller.DiscardGenerationDraft();
        result.ok = true;
        result.code = "OK";
        result.message = "已丢弃生成草稿";
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
