#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialAssetOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperationSupport.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

using MODEL_MATERIAL_OPERATIONS::MakeOperationParameter;
using MODEL_MATERIAL_OPERATIONS::RequireScene;

bool RequireMaterialDocument(
    const ModelMaterialWorkspaceController& controller,
    EditorOperationResult& result) {
  if (controller.HasMaterialDocument()) {
    return true;
  }
  result.code = "NO_MATERIAL_DOCUMENT";
  result.message = "当前没有打开的材质文档。";
  result.hints = {
      "先在材质工作区创建或打开一个材质。",
  };
  return false;
}

bool RequireEditingModel(
    const ModelMaterialWorkspaceController& controller,
    EditorOperationResult& result) {
  if (controller.IsEditingModel()) {
    return true;
  }
  result.code = "NO_MODEL_OPENED";
  result.message = "材质工作区当前没有打开模型。";
  result.hints = {
      "先用 material.open_model 后台打开模型，"
      "再查询/绑定材质槽位。",
  };
  return false;
}

bool ResolveAssetGuid(
    const nlohmann::json& parameters,
    AssetType expectedType,
    DocumentSceneBase* scene,
    const char* field,
    AssetGuid& outGuid,
    EditorOperationResult& result) {
  outGuid.value = parameters.value(field, std::string{});
  if (!outGuid.IsValid()) {
    result.code = "MISSING_FIELD";
    result.message = std::string("缺少 ") + field + " 字段。";
    return false;
  }
  const AssetRecord* record =
      scene->GetAssetDatabase().FindByGuid(outGuid);
  if (record == nullptr || record->type != expectedType) {
    result.code = "ASSET_NOT_FOUND";
    result.message = std::string("找不到 ") + field +
        " 对应资产：" + outGuid.value;
    result.hints = {
        "先用 material.list_assets 查询可用资产 guid。",
    };
    return false;
  }
  return true;
}

const char* BindingSourceName(ModelMaterialBindingSource source) {
  switch (source) {
  case ModelMaterialBindingSource::ImportedFallback:
    return "ImportedFallback";
  case ModelMaterialBindingSource::AuthoredOverride:
    return "AuthoredOverride";
  case ModelMaterialBindingSource::OverrideUnavailable:
    return "OverrideUnavailable";
  }
  return "Unknown";
}

EditorOperationResult RunDocumentEdit(
    EditorOperationResult& result,
    const std::function<bool(std::string&)>& action) {
  std::string message;
  if (action(message)) {
    result.ok = true;
    result.code = "OK";
    result.message = message;
    return result;
  }
  result.code = "EDIT_FAILED";
  result.message = message;
  return result;
}

} // namespace

void RegisterModelMaterialOperations(
    EditorOperationRegistry& registry,
    ModelMaterialWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider) {

  RegisterModelMaterialAssetOperations(
      registry, controller, sceneProvider);

  registry.Register({
      "material.open_model",
      "Open a model in the Model Material workspace in the background.",
      "model_material",
      {
          MakeOperationParameter(
              "modelGuid", EditorOperationParameterType::String,
              true, "模型资产 guid（asset.list_models 可查）"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        AssetGuid guid{};
        if (!ResolveAssetGuid(
                parameters, AssetType::Model, scene,
                "modelGuid", guid, result)) {
          return result;
        }
        std::string message;
        if (controller.OpenModel(*scene, guid, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingTransition()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前材质文档有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 material.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "OPEN_FAILED";
        result.message = message;
        return result;
      },
  });

  registry.Register({
      "material.open_asset",
      "Open a Material asset in the Model Material workspace in the background.",
      "model_material",
      {
          MakeOperationParameter(
              "materialGuid", EditorOperationParameterType::String,
              true, "材质资产 guid"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        AssetGuid guid{};
        if (!ResolveAssetGuid(
                parameters, AssetType::Material, scene,
                "materialGuid", guid, result)) {
          return result;
        }
        std::string message;
        if (controller.OpenMaterialAsset(*scene, guid, message)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          return result;
        }
        if (controller.HasPendingTransition()) {
          result.code = "PENDING_CONFIRMATION";
          result.message =
              "当前材质文档有未保存修改，编辑器已弹出确认框；"
              "需要用户在编辑器中确认后才能继续。";
          result.hints = {
              "先调用 material.save 保存，再重试本操作。",
          };
          return result;
        }
        result.code = "OPEN_FAILED";
        result.message = message;
        return result;
      },
  });

  registry.Register({
      "model.list_slots",
      "List material slots of the currently open model in the Model Material workspace.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireEditingModel(controller, result)) {
          return result;
        }
        const std::vector<ModelMaterialSlotView>& slots =
            controller.GetSlotViews();
        if (slots.empty()) {
          result.code = "NO_SLOTS";
          result.message = "当前模型没有可用的材质槽位。";
          return result;
        }
        for (const ModelMaterialSlotView& slot : slots) {
          std::string line =
              "slot: " + slot.displayName +
              " (materialIndex " +
              std::to_string(slot.materialIndex) +
              ", slotId " + std::to_string(slot.slotId.value) +
              ", surfaces " + std::to_string(slot.surfaceCount) +
              ", source " + BindingSourceName(slot.source);
          if (slot.overrideMaterialGuid.IsValid()) {
            line += ", override " + slot.overrideMaterialGuid.value;
          }
          if (!slot.diagnostic.empty()) {
            line += ", diagnostic " + slot.diagnostic;
          }
          line += ")";
          result.details.push_back(std::move(line));
        }
        result.ok = true;
        result.code = "OK";
        result.message =
            "已返回 " + std::to_string(slots.size()) +
            " 个材质槽位。";
        return result;
      },
  });

  registry.Register({
      "model.bind_material",
      "Bind a Material asset to a model slot.",
      "model_material",
      {
          MakeOperationParameter(
              "materialIndex", EditorOperationParameterType::Int,
              true, "model.list_slots 返回的 materialIndex"),
          MakeOperationParameter(
              "materialAssetGuid", EditorOperationParameterType::String,
              true, "material.list_assets 返回的 guid"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireEditingModel(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        const auto materialIndexIt = parameters.find("materialIndex");
        if (materialIndexIt == parameters.end() ||
            !materialIndexIt->is_number()) {
          result.code = "MISSING_FIELD";
          result.message = "缺少 materialIndex 字段（整数）。";
          return result;
        }
        AssetGuid materialGuid{};
        if (!ResolveAssetGuid(
                parameters, AssetType::Material, scene,
                "materialAssetGuid", materialGuid, result)) {
          return result;
        }
        const uint32_t materialIndex =
            materialIndexIt->get<uint32_t>();
        const ModelMaterialSlotView* slot = nullptr;
        for (const ModelMaterialSlotView& candidate :
             controller.GetSlotViews()) {
          if (candidate.materialIndex == materialIndex) {
            slot = &candidate;
            break;
          }
        }
        if (slot == nullptr) {
          result.code = "SLOT_NOT_FOUND";
          result.message =
              "找不到 materialIndex=" +
              std::to_string(materialIndex) + " 的槽位。";
          result.hints = {
              "先用 model.list_slots 查询当前模型的槽位。",
          };
          return result;
        }
        ModelMaterialBindingCommand command{};
        command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
        command.slotId = slot->slotId;
        command.materialIndexHint = materialIndex;
        command.materialAssetGuid = materialGuid;
        const ModelMaterialBindingCommandResult binding =
            controller.ExecuteBindingCommand(
                command,
                scene->GetAssetDatabase(),
                scene->GetAssetRegistry());
        if (!binding.accepted) {
          result.code = "BIND_FAILED";
          result.message = binding.message;
          return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message = binding.message;
        return result;
      },
  });

  registry.Register({
      "model.restore_imported_material",
      "Restore the imported model material on a slot.",
      "model_material",
      {
          MakeOperationParameter(
              "materialIndex", EditorOperationParameterType::Int,
              true, "model.list_slots 返回的 materialIndex"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireEditingModel(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        const auto materialIndexIt = parameters.find("materialIndex");
        if (materialIndexIt == parameters.end() ||
            !materialIndexIt->is_number()) {
          result.code = "MISSING_FIELD";
          result.message = "缺少 materialIndex 字段（整数）。";
          return result;
        }
        const uint32_t materialIndex =
            materialIndexIt->get<uint32_t>();
        const ModelMaterialSlotView* slot = nullptr;
        for (const ModelMaterialSlotView& candidate :
             controller.GetSlotViews()) {
          if (candidate.materialIndex == materialIndex) {
            slot = &candidate;
            break;
          }
        }
        if (slot == nullptr) {
          result.code = "SLOT_NOT_FOUND";
          result.message =
              "找不到 materialIndex=" +
              std::to_string(materialIndex) + " 的槽位。";
          result.hints = {
              "先用 model.list_slots 查询当前模型的槽位。",
          };
          return result;
        }
        ModelMaterialBindingCommand command{};
        command.type =
            ModelMaterialBindingCommandType::RestoreImportedFallback;
        command.slotId = slot->slotId;
        command.materialIndexHint = materialIndex;
        const ModelMaterialBindingCommandResult binding =
            controller.ExecuteBindingCommand(
                command,
                scene->GetAssetDatabase(),
                scene->GetAssetRegistry());
        if (!binding.accepted) {
          result.code = "RESTORE_FAILED";
          result.message = binding.message;
          return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message = binding.message;
        return result;
      },
  });

  registry.Register({
      "material.save",
      "Save the current material and/or material binding document.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        std::string message;
        if (controller.SaveMaterialDocument(*scene, message)) {
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
      "material.undo",
      "Undo the most recent material edit.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.Undo(*scene, message);
            });
      },
  });

  registry.Register({
      "material.redo",
      "Redo the most recently undone material edit.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.Redo(*scene, message);
            });
      },
  });

  registry.Register({
      "material.add_node",
      "Add a material graph node by stable node type.",
      "model_material",
      {
          MakeOperationParameter(
              "referenceId", EditorOperationParameterType::String,
              false, "Variable node runtime binding ID"),
          MakeOperationParameter(
              "nodeType", EditorOperationParameterType::String,
              true, "stable name，例如 node.additive / Constant / noise.value"),
          MakeOperationParameter(
              "name", EditorOperationParameterType::String,
              false, "可选显示名称"),
          MakeOperationParameter(
              "positionX", EditorOperationParameterType::Float,
              false, "manualPosition=true 时的画布 X"),
          MakeOperationParameter(
              "positionY", EditorOperationParameterType::Float,
              false, "manualPosition=true 时的画布 Y"),
          MakeOperationParameter(
              "manualPosition", EditorOperationParameterType::Bool,
              false, "true 使用给定位置，否则按分类自动排布"),
          MakeOperationParameter(
              "valueType", EditorOperationParameterType::String,
              false, "Constant/Parameter：Float1/Float2/Float3/Float4"),
          MakeOperationParameter(
              "valueX", EditorOperationParameterType::Float,
              false, "初始值分量"),
          MakeOperationParameter(
              "valueY", EditorOperationParameterType::Float,
              false, "初始值分量"),
          MakeOperationParameter(
              "valueZ", EditorOperationParameterType::Float,
              false, "初始值分量"),
          MakeOperationParameter(
              "valueW", EditorOperationParameterType::Float,
              false, "初始值分量"),
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        MaterialGraphNodeCreateRequest create{};
        create.nodeType = parameters.value("nodeType", std::string{});
        create.displayName = parameters.value("name", std::string{});
        create.position = {
            parameters.value("positionX", 0.0f),
            parameters.value("positionY", 0.0f),
        };
        create.manualPosition =
            parameters.value("manualPosition", false);
        create.valueType =
            parameters.value("valueType", std::string{});
        const bool hasValues =
            parameters.contains("valueX") ||
            parameters.contains("valueY") ||
            parameters.contains("valueZ") ||
            parameters.contains("valueW");
        create.valueSet = hasValues;
        create.valueX = parameters.value("valueX", 0.0f);
        create.valueY = parameters.value("valueY", 0.0f);
        create.valueZ = parameters.value("valueZ", 0.0f);
        create.valueW = parameters.value("valueW", 0.0f);
        create.referenceId =
            parameters.value("referenceId", std::string{});
        create.allowFunctionCall = false;
        std::string message;
        std::vector<std::string> details;
        std::string code;
        if (controller.AddMaterialGraphNode(
                *scene, create, message, details, code)) {
          result.ok = true;
          result.code = "OK";
          result.message = message;
          result.details = std::move(details);
        } else {
          result.code = code;
          result.message = message;
        }
        return result;
      },
  });

  registry.Register({
      "material.duplicate_selected",
      "Duplicate the selected material graph nodes.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.DuplicateSelectedGraphNodes(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.delete_selected",
      "Delete the selected material graph nodes.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.DeleteSelectedGraphNodes(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.delete_connection",
      "Delete the selected material graph connection.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.DeleteSelectedGraphEdge(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.group_selected",
      "Create a graph group from the selected nodes.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.CreateGraphGroupFromSelection(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.ungroup_selected",
      "Remove graph groups that contain the selected nodes.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.UngroupSelectedGraphNodes(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.arrange_selected",
      "Arrange the selected material graph nodes into a grid.",
      "model_material",
      {},
      [&controller, sceneProvider](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        return RunDocumentEdit(
            result,
            [&controller, scene](std::string& message) {
              return controller.ArrangeSelectedGraphNodes(
                  *scene, message);
            });
      },
  });

  registry.Register({
      "material.frame_selection",
      "Frame the selected material graph node in the canvas.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.FrameMaterialGraph(
            true, MATH::Vec2{ 1280.0f, 720.0f });
        result.ok = true;
        result.code = "OK";
        result.message = "已聚焦选中节点。";
        return result;
      },
  });

  registry.Register({
      "material.frame_all",
      "Frame every material graph node in the canvas.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.FrameMaterialGraph(
            false, MATH::Vec2{ 1280.0f, 720.0f });
        result.ok = true;
        result.code = "OK";
        result.message = "已聚焦全部节点。";
        return result;
      },
  });

  registry.Register({
      "material.frame_preview",
      "Frame the preview camera on the current model/material.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.FramePreviewCamera();
        result.ok = true;
        result.code = "OK";
        result.message = "已聚焦预览相机。";
        return result;
      },
  });

  registry.Register({
      "material.collapse_selected",
      "Collapse the selected material graph nodes.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.SetSelectedGraphNodesCollapsed(true);
        result.ok = true;
        result.code = "OK";
        result.message = "已折叠选中节点。";
        return result;
      },
  });

  registry.Register({
      "material.expand_selected",
      "Expand the selected material graph nodes.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.SetSelectedGraphNodesCollapsed(false);
        result.ok = true;
        result.code = "OK";
        result.message = "已展开选中节点。";
        return result;
      },
  });

  registry.Register({
      "material.collapse_all",
      "Collapse every material graph node.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.SetAllGraphNodesCollapsed(true);
        result.ok = true;
        result.code = "OK";
        result.message = "已折叠全部节点。";
        return result;
      },
  });

  registry.Register({
      "material.expand_all",
      "Expand every material graph node.",
      "model_material",
      {},
      [&controller](const nlohmann::json&) {
        EditorOperationResult result{};
        if (!RequireMaterialDocument(controller, result)) {
          return result;
        }
        controller.SetAllGraphNodesCollapsed(false);
        result.ok = true;
        result.code = "OK";
        result.message = "已展开全部节点。";
        return result;
      },
  });
}

} // namespace HIKARI::EDITOR
