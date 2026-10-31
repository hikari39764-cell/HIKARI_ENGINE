#include "HIKARI_MaterialGraphAuthoringOperations.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"

#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialAssetOperations.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

bool RequireMaterialDocument(const ModelMaterialWorkspaceController& controller, EditorOperationResult& result) {
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

bool RequireEditingModel(const ModelMaterialWorkspaceController& controller, EditorOperationResult& result) {
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

bool ResolveAssetGuid(const nlohmann::json& parameters, AssetType expectedType, DocumentSceneBase* scene,
                      const char* field, AssetGuid& outGuid, EditorOperationResult& result) {
    const std::string text = parameters.value(field, std::string{});
    const AssetRecord* record = FindOperationAsset(scene->GetAssetDatabase(), text, expectedType);
    if (record == nullptr) {
        result.code = "ASSET_NOT_FOUND";
        result.message = std::string("找不到 ") + field + " 对应资产：" + text;
        result.hints = {
            "先用 material.list_assets 查询可用资产 guid。",
        };
        return false;
    }
    outGuid = record->guid;
    return true;
}

const char* BindingSourceName(ModelMaterialBindingSource source) {
    switch (source) {
    case ModelMaterialBindingSource::ImportedFallback: return "ImportedFallback";
    case ModelMaterialBindingSource::AuthoredOverride: return "AuthoredOverride";
    case ModelMaterialBindingSource::OverrideUnavailable: return "OverrideUnavailable";
    }
    return "Unknown";
}

} // namespace

void RegisterModelMaterialOperations(EditorOperationRegistry& registry, ModelMaterialWorkspaceController& controller,
                                     std::function<DocumentSceneBase*()> sceneProvider) {
    RegisterMaterialGraphAuthoringOperations(registry, controller, sceneProvider);

    RegisterModelMaterialAssetOperations(registry, controller, sceneProvider);

    using Controller = ModelMaterialWorkspaceController;
    const auto registerEdit = [&](const char* id, const char* summary,
                                  bool (Controller::*action)(DocumentSceneBase&, std::string&),
                                  const char* failure = "EDIT_FAILED") {
        registry.Register({id,
                           summary,
                           "model_material",
                           {},
                           [&controller, sceneProvider, action, failure](const nlohmann::json&) {
                               EditorOperationResult result;
                               DocumentSceneBase* scene = nullptr;
                               if (!RequireMaterialDocument(controller, result) ||
                                   !RequireScene(sceneProvider, result, scene))
                                   return result;
                               result.ok = (controller.*action)(*scene, result.message);
                               result.code = result.ok ? "OK" : failure;
                               return result;
                           }},
                          EditorOperationKind::Mutation);
    };
    const auto registerSession = [&](const char* id, const char* summary, const char* message, auto action) {
        registry.Register({id,
                           summary,
                           "model_material",
                           {},
                           [&controller, action, message](const nlohmann::json&) {
                               EditorOperationResult result;
                               if (!RequireMaterialDocument(controller, result))
                                   return result;
                               action(controller);
                               result.ok = true;
                               result.code = "OK";
                               result.message = message;
                               return result;
                           }},
                          EditorOperationKind::Session);
    };

    registry.Register(
        {
            "material.open_model",
            "Open a model in the Model Material workspace in the background.",
            "model_material",
            {
                EditorOperationParameter{"modelGuid", EditorOperationParameterType::String, true,
                                         "模型资产：GUID（asset.query 可查）或项目路径"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) {
                    return result;
                }
                AssetGuid guid{};
                if (!ResolveAssetGuid(parameters, AssetType::Model, scene, "modelGuid", guid, result)) {
                    return result;
                }
                std::string message;
                if (controller.OpenModel(*scene, guid, message)) {
                    return OperationOk(std::move(message));
                }
                if (controller.HasPendingTransition()) {
                    result.code = "PENDING_CONFIRMATION";
                    result.message = "当前材质文档有未保存修改，编辑器已弹出确认框；"
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
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "material.open_asset",
            "Open a Material asset in the Model Material workspace in the background.",
            "model_material",
            {
                EditorOperationParameter{"materialGuid", EditorOperationParameterType::String, true,
                                         "材质资产：GUID 或项目路径"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) {
                    return result;
                }
                AssetGuid guid{};
                if (!ResolveAssetGuid(parameters, AssetType::Material, scene, "materialGuid", guid, result)) {
                    return result;
                }
                std::string message;
                if (controller.OpenMaterialAsset(*scene, guid, message)) {
                    return OperationOk(std::move(message));
                }
                if (controller.HasPendingTransition()) {
                    result.code = "PENDING_CONFIRMATION";
                    result.message = "当前材质文档有未保存修改，编辑器已弹出确认框；"
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
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "model.list_slots",
            "List material slots of the currently open model in the Model Material workspace.",
            "model_material",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireEditingModel(controller, result)) {
                    return result;
                }
                const std::vector<ModelMaterialSlotView>& slots = controller.GetSlotViews();
                if (slots.empty()) {
                    return OperationFailure("NO_SLOTS", "当前模型没有可用的材质槽位。");
                }
                for (const ModelMaterialSlotView& slot : slots) {
                    std::string line = "slot: " + slot.displayName + " (materialIndex " +
                                       std::to_string(slot.materialIndex) + ", slotId " +
                                       std::to_string(slot.slotId.value) + ", surfaces " +
                                       std::to_string(slot.surfaceCount) + ", source " + BindingSourceName(slot.source);
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
                result.message = "已返回 " + std::to_string(slots.size()) + " 个材质槽位。";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);

    registry.Register(
        {
            "model.bind_material",
            "Bind a Material asset to a model slot.",
            "model_material",
            {
                EditorOperationParameter{"materialIndex", EditorOperationParameterType::Int, true,
                                         "model.list_slots 返回的 materialIndex"},
                EditorOperationParameter{"materialAssetGuid", EditorOperationParameterType::String, true,
                                         "材质资产：GUID（material.list_assets 可查）或项目路径"},
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
                if (materialIndexIt == parameters.end() || !materialIndexIt->is_number()) {
                    return OperationFailure("MISSING_FIELD", "缺少 materialIndex 字段（整数）。");
                }
                AssetGuid materialGuid{};
                if (!ResolveAssetGuid(parameters, AssetType::Material, scene, "materialAssetGuid", materialGuid,
                                      result)) {
                    return result;
                }
                const uint32_t materialIndex = materialIndexIt->get<uint32_t>();
                const ModelMaterialSlotView* slot = nullptr;
                for (const ModelMaterialSlotView& candidate : controller.GetSlotViews()) {
                    if (candidate.materialIndex == materialIndex) {
                        slot = &candidate;
                        break;
                    }
                }
                if (slot == nullptr) {
                    result.code = "SLOT_NOT_FOUND";
                    result.message = "找不到 materialIndex=" + std::to_string(materialIndex) + " 的槽位。";
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
                    controller.ExecuteBindingCommand(command, scene->GetAssetDatabase(), scene->GetAssetRegistry());
                return OperationResult(binding.accepted, "BIND_FAILED", binding.message);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "model.restore_imported_material",
            "Restore the imported model material on a slot.",
            "model_material",
            {
                EditorOperationParameter{"materialIndex", EditorOperationParameterType::Int, true,
                                         "model.list_slots 返回的 materialIndex"},
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
                if (materialIndexIt == parameters.end() || !materialIndexIt->is_number()) {
                    return OperationFailure("MISSING_FIELD", "缺少 materialIndex 字段（整数）。");
                }
                const uint32_t materialIndex = materialIndexIt->get<uint32_t>();
                const ModelMaterialSlotView* slot = nullptr;
                for (const ModelMaterialSlotView& candidate : controller.GetSlotViews()) {
                    if (candidate.materialIndex == materialIndex) {
                        slot = &candidate;
                        break;
                    }
                }
                if (slot == nullptr) {
                    result.code = "SLOT_NOT_FOUND";
                    result.message = "找不到 materialIndex=" + std::to_string(materialIndex) + " 的槽位。";
                    result.hints = {
                        "先用 model.list_slots 查询当前模型的槽位。",
                    };
                    return result;
                }
                ModelMaterialBindingCommand command{};
                command.type = ModelMaterialBindingCommandType::RestoreImportedFallback;
                command.slotId = slot->slotId;
                command.materialIndexHint = materialIndex;
                const ModelMaterialBindingCommandResult binding =
                    controller.ExecuteBindingCommand(command, scene->GetAssetDatabase(), scene->GetAssetRegistry());
                return OperationResult(binding.accepted, "RESTORE_FAILED", binding.message);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registerEdit("material.save", "Save the current material and/or material binding document.",
                 &Controller::SaveMaterialDocument, "SAVE_FAILED");

    registerEdit("material.undo", "Undo the most recent material edit.", &Controller::Undo);

    registerEdit("material.redo", "Redo the most recently undone material edit.", &Controller::Redo);

    registry.Register(
        {
            "material.add_node",
            "Add a material graph node by stable node type.",
            "model_material",
            {
                EditorOperationParameter{"referenceId", EditorOperationParameterType::String, false,
                                         "Variable node runtime binding ID"},
                EditorOperationParameter{"nodeType", EditorOperationParameterType::String, true,
                                         "stable name，例如 node.additive / Constant / noise.value"},
                EditorOperationParameter{"name", EditorOperationParameterType::String, false, "可选显示名称"},
                EditorOperationParameter{"positionX", EditorOperationParameterType::Float, false,
                                         "manualPosition=true 时的画布 X"},
                EditorOperationParameter{"positionY", EditorOperationParameterType::Float, false,
                                         "manualPosition=true 时的画布 Y"},
                EditorOperationParameter{"manualPosition", EditorOperationParameterType::Bool, false,
                                         "true 使用给定位置，否则按分类自动排布"},
                EditorOperationParameter{"valueType", EditorOperationParameterType::String, false,
                                         "Constant/Parameter：Float1/Float2/Float3/Float4"},
                EditorOperationParameter{"valueX", EditorOperationParameterType::Float, false, "初始值分量"},
                EditorOperationParameter{"valueY", EditorOperationParameterType::Float, false, "初始值分量"},
                EditorOperationParameter{"valueZ", EditorOperationParameterType::Float, false, "初始值分量"},
                EditorOperationParameter{"valueW", EditorOperationParameterType::Float, false, "初始值分量"},
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
                create.manualPosition = parameters.value("manualPosition", false);
                create.valueType = parameters.value("valueType", std::string{});
                const bool hasValues = parameters.contains("valueX") || parameters.contains("valueY") ||
                                       parameters.contains("valueZ") || parameters.contains("valueW");
                create.valueSet = hasValues;
                create.valueX = parameters.value("valueX", 0.0f);
                create.valueY = parameters.value("valueY", 0.0f);
                create.valueZ = parameters.value("valueZ", 0.0f);
                create.valueW = parameters.value("valueW", 0.0f);
                create.referenceId = parameters.value("referenceId", std::string{});
                create.allowFunctionCall = false;
                std::string message;
                std::vector<std::string> details;
                std::string code;
                if (controller.AddMaterialGraphNode(*scene, create, message, details, code)) {
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
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registerEdit("material.duplicate_selected", "Duplicate the selected material graph nodes.",
                 &Controller::DuplicateSelectedGraphNodes);

    registerEdit("material.delete_selected", "Delete the selected material graph nodes.",
                 &Controller::DeleteSelectedGraphNodes);

    registerEdit("material.delete_connection", "Delete the selected material graph connection.",
                 &Controller::DeleteSelectedGraphEdge);

    registerEdit("material.group_selected", "Create a graph group from the selected nodes.",
                 &Controller::CreateGraphGroupFromSelection);

    registerEdit("material.ungroup_selected", "Remove graph groups that contain the selected nodes.",
                 &Controller::UngroupSelectedGraphNodes);

    registerEdit("material.arrange_selected", "Arrange selected nodes by connections without moving other nodes.",
                 &Controller::ArrangeSelectedGraphNodes);

    registerSession("material.frame_selection", "Frame the selected material graph node in the canvas.",
                    "已聚焦选中节点。", [](Controller& editor) { editor.FrameMaterialGraph(true, {1280.0f, 720.0f}); });

    registerSession("material.frame_all", "Frame every material graph node in the canvas.", "已聚焦全部节点。",
                    [](Controller& editor) { editor.FrameMaterialGraph(false, {1280.0f, 720.0f}); });

    registerSession("material.frame_preview", "Frame the preview camera on the current model/material.",
                    "已聚焦预览相机。", [](Controller& editor) { editor.FramePreviewCamera(); });

    registerSession("material.collapse_selected", "Collapse the selected material graph nodes.", "已折叠选中节点。",
                    [](Controller& editor) { editor.SetSelectedGraphNodesCollapsed(true); });

    registerSession("material.expand_selected", "Expand the selected material graph nodes.", "已展开选中节点。",
                    [](Controller& editor) { editor.SetSelectedGraphNodesCollapsed(false); });

    registerSession("material.collapse_all", "Collapse every material graph node.", "已折叠全部节点。",
                    [](Controller& editor) { editor.SetAllGraphNodesCollapsed(true); });

    registerSession("material.expand_all", "Expand every material graph node.", "已展开全部节点。",
                    [](Controller& editor) { editor.SetAllGraphNodesCollapsed(false); });
}

} // namespace HIKARI::EDITOR
