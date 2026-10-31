#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineOperations.h"

#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/Text/HIKARI_ParseNumber.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

bool RequireAsmDocument(const AnimationStateMachineWorkspaceController& controller, EditorOperationResult& result) {
    if (controller.IsDocumentOpen()) {
        return true;
    }
    result.code = "NO_ASM_DOCUMENT";
    result.message = "动画状态机工作区未打开文档";
    return false;
}

bool ResolveStateId(const ANIMATION::AnimationStateMachineDefinition& definition, const std::string& value,
                    ANIMATION::AnimationStateId& outId, EditorOperationResult& result) {
    if (value.empty()) {
        result.code = "MISSING_FIELD";
        result.message = "缺少 state 字段（状态 id 或名称）";
        return false;
    }
    uint32_t numericId = 0u;
    if (TEXT::ParseNumber(value, numericId)) {
        outId.value = numericId;
        if (ANIMATION::FindAnimationState(definition, outId) != nullptr) {
            return true;
        }
        result.code = "STATE_NOT_FOUND";
        result.message = "找不到状态 id：" + value;
        return false;
    }
    for (const ANIMATION::AnimationState& state : definition.states) {
        if (state.name == value) {
            outId = state.id;
            return true;
        }
    }
    result.code = "STATE_NOT_FOUND";
    result.message = "找不到状态：" + value;
    return false;
}

bool ResolveParameterId(const ANIMATION::AnimationStateMachineDefinition& definition, const std::string& value,
                        ANIMATION::AnimationParameterId& outId, EditorOperationResult& result) {
    if (value.empty()) {
        result.code = "MISSING_FIELD";
        result.message = "缺少 parameter 字段（参数 id 或名称）";
        return false;
    }
    uint32_t numericId = 0u;
    if (TEXT::ParseNumber(value, numericId)) {
        outId.value = numericId;
        if (ANIMATION::FindAnimationParameter(definition, outId) != nullptr) {
            return true;
        }
        result.code = "PARAMETER_NOT_FOUND";
        result.message = "找不到参数 id：" + value;
        return false;
    }
    for (const ANIMATION::AnimationParameterDefinition& parameter : definition.parameters) {
        if (parameter.name == value) {
            outId = parameter.id;
            return true;
        }
    }
    result.code = "PARAMETER_NOT_FOUND";
    result.message = "找不到参数：" + value;
    return false;
}

bool ParseParameterType(const nlohmann::json& parameters, ANIMATION::AnimationParameterType& outType,
                        EditorOperationResult& result) {
    const std::string type = parameters.value("type", std::string{});
    if (type == "bool") {
        outType = ANIMATION::AnimationParameterType::Bool;
    } else if (type == "float") {
        outType = ANIMATION::AnimationParameterType::Float;
    } else if (type == "int" || type == "integer") {
        outType = ANIMATION::AnimationParameterType::Integer;
    } else if (type == "trigger") {
        outType = ANIMATION::AnimationParameterType::Trigger;
    } else {
        result.code = "UNKNOWN_PARAMETER_TYPE";
        result.message = "未知的参数类型：" + type;
        result.details = {
            "supported: bool / float / int / trigger",
        };
        return false;
    }
    return true;
}

const char* ParameterTypeName(ANIMATION::AnimationParameterType type) {
    switch (type) {
    case ANIMATION::AnimationParameterType::Bool: return "bool";
    case ANIMATION::AnimationParameterType::Float: return "float";
    case ANIMATION::AnimationParameterType::Integer: return "int";
    case ANIMATION::AnimationParameterType::Trigger: return "trigger";
    }
    return "unknown";
}

void AppendQuerySummary(const ANIMATION::AnimationStateMachineDefinition& definition, EditorOperationResult& result) {
    result.details.push_back("name: " + definition.name);
    result.details.push_back("entryStateId: " + std::to_string(definition.entryStateId.value));
    for (const ANIMATION::AnimationState& state : definition.states) {
        result.details.push_back("state: " + state.name + " (id " + std::to_string(state.id.value) + ")");
    }
    for (const ANIMATION::AnimationParameterDefinition& parameter : definition.parameters) {
        result.details.push_back("parameter: " + parameter.name + " (id " + std::to_string(parameter.id.value) + ", " +
                                 ParameterTypeName(parameter.type) + ")");
    }
    for (const ANIMATION::AnimationStateTransition& transition : definition.transitions) {
        result.details.push_back("transition: id " + std::to_string(transition.id.value) + " (" +
                                 std::to_string(transition.sourceStateId.value) + " -> " +
                                 std::to_string(transition.targetStateId.value) + ")");
    }
}

} // namespace

void RegisterAnimationStateMachineOperations(EditorOperationRegistry& registry,
                                             AnimationStateMachineWorkspaceController& controller,
                                             std::function<DocumentSceneBase*()> sceneProvider) {
    {
        EditorOperationDefinition transition{
            "asm.new_asset",
            "Create a new Animation State Machine document in the background.",
            "animation_state_machine",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                std::string message;
                if (controller.NewAsset(message)) {
                    return OperationOk(std::move(message));
                }
                if (controller.IsDocumentDirty()) {
                    result.code = "PENDING_CONFIRMATION";
                    result.message = "当前状态机有未保存修改，编辑器已弹出确认框；"
                                     "需要用户在编辑器中确认后才能继续。";
                    result.hints = {
                        "先调用 asm.save 保存，再重试本操作。",
                    };
                    return result;
                }
                result.code = "NEW_ASSET_FAILED";
                result.message = message;
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }

    {
        EditorOperationDefinition transition{
            "asm.open_asset",
            "Open an Animation State Machine asset in the background.",
            "animation_state_machine",
            {
                EditorOperationParameter{"assetGuid", EditorOperationParameterType::String, true, "状态机资产 guid"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) {
                    return result;
                }
                const std::string guidText = parameters.value("assetGuid", std::string{});
                const AssetGuid guid{guidText};
                if (!guid.IsValid()) {
                    return OperationFailure("MISSING_FIELD", "缺少 assetGuid 字段。");
                }
                const AssetRecord* record = scene->GetAssetDatabase().FindByGuid(guid);
                if (record == nullptr || record->type != AssetType::AnimationStateMachine) {
                    result.code = "ASSET_NOT_FOUND";
                    result.message = "找不到该状态机资产：" + guidText;
                    result.hints = {
                        "状态机资产列表暂未开放查询，"
                        "可在动画状态机工作区的 Open Asset 菜单查看。",
                    };
                    return result;
                }
                std::string message;
                if (controller.OpenAsset(scene->GetAssetDatabase(), guid, message)) {
                    result.ok = true;
                    result.code = "OK";
                    result.message = message;
                    return result;
                }
                if (controller.IsDocumentDirty()) {
                    result.code = "PENDING_CONFIRMATION";
                    result.message = "当前状态机有未保存修改，编辑器已弹出确认框；"
                                     "需要用户在编辑器中确认后才能继续。";
                    result.hints = {
                        "先调用 asm.save 保存，再重试本操作。",
                    };
                    return result;
                }
                result.code = "OPEN_FAILED";
                result.message = message;
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }

    registry.Register(
        {
            "asm.save",
            "Save the current animation state machine document.",
            "animation_state_machine",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) {
                    return result;
                }
                std::string message;
                return OperationResult(controller.Save(*scene, message), "SAVE_FAILED", std::move(message));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.undo",
            "Undo the most recent animation state machine edit.",
            "animation_state_machine",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                std::string message;
                return OperationResult(controller.Undo(message), "UNDO_FAILED", std::move(message));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.redo",
            "Redo the most recently undone animation state machine edit.",
            "animation_state_machine",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                std::string message;
                return OperationResult(controller.Redo(message), "REDO_FAILED", std::move(message));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.query",
            "Return states, parameters and transitions of the current state machine.",
            "animation_state_machine",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.message = "状态机概要：";
                AppendQuerySummary(controller.GetDefinition(), result);
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);

    registry.Register(
        {
            "asm.add_state",
            "Add a state to the current animation state machine.",
            "animation_state_machine",
            {
                EditorOperationParameter{"x", EditorOperationParameterType::Float, false, "图中坐标 X，缺省自动排布"},
                EditorOperationParameter{"y", EditorOperationParameterType::Float, false, "图中坐标 Y，缺省自动排布"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                const auto x = parameters.find("x");
                const auto y = parameters.find("y");
                if (x != parameters.end() && y != parameters.end() && x->is_number() && y->is_number()) {
                    controller.AddStateAt(x->get<float>(), y->get<float>());
                } else {
                    controller.AddState();
                }
                return OperationOk("已添加状态");
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.delete_state",
            "Delete a state and its transitions by id or name.",
            "animation_state_machine",
            {
                EditorOperationParameter{"state", EditorOperationParameterType::String, true, "状态 id 或名称"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                ANIMATION::AnimationStateId stateId{};
                if (!ResolveStateId(controller.GetDefinition(), parameters.value("state", std::string{}), stateId,
                                    result)) {
                    return result;
                }
                std::string message;
                if (controller.DeleteState(stateId, message)) {
                    result.ok = true;
                    result.code = "OK";
                    result.message = "已删除状态";
                } else {
                    result.code = "DELETE_FAILED";
                    result.message = message;
                }
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.add_parameter",
            "Add a parameter (bool/float/int/trigger) to the state machine.",
            "animation_state_machine",
            {
                EditorOperationParameter{"type", EditorOperationParameterType::String, true, "bool/float/int/trigger"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                ANIMATION::AnimationParameterType type = ANIMATION::AnimationParameterType::Bool;
                if (!ParseParameterType(parameters, type, result)) {
                    return result;
                }
                controller.AddParameter(type);
                return OperationOk("已添加参数：" + parameters.value("type", std::string{}));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.delete_parameter",
            "Delete a parameter and its transition conditions by id or name.",
            "animation_state_machine",
            {
                EditorOperationParameter{"parameter", EditorOperationParameterType::String, true, "参数 id 或名称"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                ANIMATION::AnimationParameterId parameterId{};
                if (!ResolveParameterId(controller.GetDefinition(), parameters.value("parameter", std::string{}),
                                        parameterId, result)) {
                    return result;
                }
                std::string message;
                if (controller.DeleteParameter(parameterId, message)) {
                    result.ok = true;
                    result.code = "OK";
                    result.message = "已删除参数";
                } else {
                    result.code = "DELETE_FAILED";
                    result.message = message;
                }
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.add_transition",
            "Add a transition between two states by id or name.",
            "animation_state_machine",
            {
                EditorOperationParameter{"source", EditorOperationParameterType::String, true, "源状态 id 或名称"},
                EditorOperationParameter{"target", EditorOperationParameterType::String, true, "目标状态 id 或名称"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                const auto& definition = controller.GetDefinition();
                ANIMATION::AnimationStateId sourceId{};
                ANIMATION::AnimationStateId targetId{};
                if (!ResolveStateId(definition, parameters.value("source", std::string{}), sourceId, result) ||
                    !ResolveStateId(definition, parameters.value("target", std::string{}), targetId, result)) {
                    return result;
                }
                if (sourceId == targetId) {
                    return OperationFailure("INVALID_TRANSITION", "源状态和目标状态不能相同");
                }
                controller.AddTransitionBetween(sourceId, targetId);
                return OperationOk("已添加过渡");
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.delete_transition",
            "Delete a transition by its numeric id.",
            "animation_state_machine",
            {
                EditorOperationParameter{"transition", EditorOperationParameterType::String, true,
                                         "过渡 id（asm.query 可查询）"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                const std::string transitionText = parameters.value("transition", std::string{});
                uint32_t numericId = 0u;
                if (!TEXT::ParseNumber(transitionText, numericId)) {
                    return OperationFailure("MISSING_FIELD", "transition 需要数字 id");
                }
                std::string message;
                if (controller.DeleteTransition(ANIMATION::AnimationTransitionId{numericId}, message)) {
                    result.ok = true;
                    result.code = "OK";
                    result.message = "已删除过渡";
                } else {
                    result.code = "DELETE_FAILED";
                    result.message = message;
                }
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.set_state_property",
            "Update name/speed/loop of a state by id or name.",
            "animation_state_machine",
            {
                EditorOperationParameter{"state", EditorOperationParameterType::String, true, "状态 id 或名称"},
                EditorOperationParameter{"name", EditorOperationParameterType::String, false, "新名称"},
                EditorOperationParameter{"speed", EditorOperationParameterType::Float, false, "播放速度"},
                EditorOperationParameter{"loop", EditorOperationParameterType::Bool, false, "是否循环"},
            },
            [&controller](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                ANIMATION::AnimationStateId stateId{};
                if (!ResolveStateId(controller.GetDefinition(), parameters.value("state", std::string{}), stateId,
                                    result)) {
                    return result;
                }
                std::optional<std::string> name{};
                const auto nameIt = parameters.find("name");
                if (nameIt != parameters.end() && nameIt->is_string()) {
                    name = nameIt->get<std::string>();
                }
                std::optional<float> speed{};
                const auto speedIt = parameters.find("speed");
                if (speedIt != parameters.end() && speedIt->is_number()) {
                    speed = speedIt->get<float>();
                }
                std::optional<bool> loop{};
                const auto loopIt = parameters.find("loop");
                if (loopIt != parameters.end() && loopIt->is_boolean()) {
                    loop = loopIt->get<bool>();
                }
                if (!name.has_value() && !speed.has_value() && !loop.has_value()) {
                    return OperationFailure("MISSING_FIELD", "至少提供一个要修改的字段（name/speed/loop）");
                }
                std::string message;
                return OperationResult(controller.EditStateProperties(stateId, name, speed, loop, message),
                                       "EDIT_FAILED", std::move(message));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "asm.delete_selected",
            "Delete the currently selected state, parameter or transition.",
            "animation_state_machine",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireAsmDocument(controller, result)) {
                    return result;
                }
                std::string message;
                return OperationResult(controller.DeleteSelected(message), "DELETE_FAILED", std::move(message));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
