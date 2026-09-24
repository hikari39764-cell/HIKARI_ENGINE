#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingOperations.h"

#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

EditorOperationParameter MakeParameter(std::string name, EditorOperationParameterType type, bool required,
                                       std::string description) {
    return {
        std::move(name),
        type,
        required,
        std::move(description),
    };
}

bool RequireContext(RuntimeBindingWorkspaceController& controller,
                    const std::function<DocumentSceneBase*()>& sceneProvider, EditorOperationResult& result,
                    DocumentSceneBase*& outScene) {
    outScene = sceneProvider ? sceneProvider() : nullptr;
    if (outScene == nullptr) {
        result.code = "NO_SCENE";
        result.message = "Runtime binding operations require an open project";
        return false;
    }
    std::string message{};
    if (!controller.Activate(*outScene, message)) {
        result.code = "CATALOG_OPEN_FAILED";
        result.message = message;
        return false;
    }
    return true;
}

nlohmann::json WriteReference(const RuntimeBindingReference& reference) {
    return {
        {"targetKind", reference.targetKind},   {"targetId", reference.targetId},
        {"targetLabel", reference.targetLabel}, {"slotId", reference.slotId},
        {"slotLabel", reference.slotLabel},     {"access", reference.access},
        {"status", reference.status},
    };
}

nlohmann::json WriteVariableSummary(const BINDING::VariableDefinition& variable) {
    return {
        {"kind", "variable"},
        {"id", variable.id},
        {"name", variable.name},
        {"type", BINDING::ToString(variable.defaultValue.type)},
    };
}

nlohmann::json WriteEventSummary(const BINDING::EventDefinition& event) {
    return {
        {"kind", "event"},
        {"id", event.id},
        {"name", event.name},
        {"hasPayload", event.hasPayload},
        {"payloadType",
         event.hasPayload ? nlohmann::json(BINDING::ToString(event.payloadType)) : nlohmann::json(nullptr)},
    };
}

nlohmann::json DescribeVariable(const BINDING::VariableDefinition& variable, size_t referenceCount) {
    nlohmann::json properties = nlohmann::json::array({
        {
            {"id", "name"},
            {"type", "string"},
            {"value", variable.name},
        },
        {
            {"id", "description"},
            {"type", "string"},
            {"value", variable.description},
        },
        {
            {"id", "type"},
            {"type", "choice"},
            {"value", BINDING::ToString(variable.defaultValue.type)},
            {"choices", {"Bool", "Int", "Float", "Float2", "Float3", "Float4", "Color"}},
        },
        {
            {"id", "default"},
            {"type", "typedValue"},
            {"valueType", BINDING::ToString(variable.defaultValue.type)},
            {"value", WriteRuntimeBindingEditorValue(variable.defaultValue)},
        },
        {
            {"id", "hasRange"},
            {"type", "bool"},
            {"value", variable.hasRange},
        },
        {
            {"id", "range"},
            {"type", "object"},
            {"value",
             {
                 {"enabled", variable.hasRange},
                 {"minimum", variable.minimum},
                 {"maximum", variable.maximum},
                 {"step", variable.step},
             }},
        },
        {
            {"id", "minimum"},
            {"type", "float"},
            {"value", variable.minimum},
        },
        {
            {"id", "maximum"},
            {"type", "float"},
            {"value", variable.maximum},
        },
        {
            {"id", "step"},
            {"type", "float"},
            {"value", variable.step},
        },
    });
    return {
        {"kind", "variable"},
        {"id", variable.id},
        {"referenceCount", referenceCount},
        {"properties", std::move(properties)},
    };
}

nlohmann::json DescribeEvent(const BINDING::EventDefinition& event, size_t referenceCount) {
    nlohmann::json properties = nlohmann::json::array({
        {
            {"id", "name"},
            {"type", "string"},
            {"value", event.name},
        },
        {
            {"id", "description"},
            {"type", "string"},
            {"value", event.description},
        },
        {
            {"id", "hasPayload"},
            {"type", "bool"},
            {"value", event.hasPayload},
        },
        {
            {"id", "payloadType"},
            {"type", "choice"},
            {"value", BINDING::ToString(event.payloadType)},
            {"choices", {"Bool", "Int", "Float", "Float2", "Float3", "Float4", "Color"}},
        },
    });
    return {
        {"kind", "event"},
        {"id", event.id},
        {"referenceCount", referenceCount},
        {"properties", std::move(properties)},
    };
}

} // namespace

void RegisterRuntimeBindingOperations(EditorOperationRegistry& registry, RuntimeBindingWorkspaceController& controller,
                                      std::function<DocumentSceneBase*()> sceneProvider) {
    registry.Register(
        {
            "binding.catalog.query",
            "List project runtime variables and events by stable ID.",
            "runtime_binding",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                (void)scene;
                result.data["variables"] = nlohmann::json::array();
                result.data["events"] = nlohmann::json::array();
                for (const BINDING::VariableDefinition& variable : controller.GetCatalog().variables) {
                    result.data["variables"].push_back(WriteVariableSummary(variable));
                }
                for (const BINDING::EventDefinition& event : controller.GetCatalog().events) {
                    result.data["events"].push_back(WriteEventSummary(event));
                }
                result.ok = true;
                result.code = "OK";
                result.message = "Runtime binding catalog returned";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);

    registry.Register(
        {
            "binding.item.describe",
            "Describe one runtime variable or event and its editable properties.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime binding ID"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                (void)scene;
                const std::string id = parameters.value("id", std::string{});
                const size_t referenceCount = controller.GetReferences(id).size();
                if (const BINDING::VariableDefinition* variable = controller.GetCatalog().FindVariable(id)) {
                    result.data = DescribeVariable(*variable, referenceCount);
                } else if (const BINDING::EventDefinition* event = controller.GetCatalog().FindEvent(id)) {
                    result.data = DescribeEvent(*event, referenceCount);
                } else {
                    result.code = "ITEM_NOT_FOUND";
                    result.message = "Runtime binding item not found: " + id;
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.message = "Runtime binding item described";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);

    registry.Register(
        {
            "binding.variable.create",
            "Create a typed project runtime variable with a stable ID.",
            "runtime_binding",
            {
                MakeParameter("type", EditorOperationParameterType::String, true,
                              "Bool, Int, Float, Float2, Float3, Float4, or Color"),
                MakeParameter("name", EditorOperationParameterType::String, false, "Display name"),
                MakeParameter("id", EditorOperationParameterType::String, false,
                              "Optional explicit stable ID; generated when omitted"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                BINDING::ValueType type{};
                if (!TryParseRuntimeBindingEditorValueType(parameters.value("type", std::string{}), type)) {
                    return OperationFailure("UNKNOWN_VALUE_TYPE", "Unknown runtime variable type");
                }
                std::string id{};
                if (!controller.CreateVariable(*scene, type, parameters.value("name", std::string{}),
                                               parameters.value("id", std::string{}), id, result.message)) {
                    result.code = "CREATE_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"kind", "variable"},
                    {"id", id},
                    {"workspace", "runtime_binding"},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.event.create",
            "Create a project runtime event with an optional typed payload.",
            "runtime_binding",
            {
                MakeParameter("name", EditorOperationParameterType::String, false, "Display name"),
                MakeParameter("id", EditorOperationParameterType::String, false,
                              "Optional explicit stable ID; generated when omitted"),
                MakeParameter("hasPayload", EditorOperationParameterType::Bool, false,
                              "Whether this event carries a typed payload"),
                MakeParameter("payloadType", EditorOperationParameterType::String, false,
                              "Bool, Int, Float, Float2, Float3, Float4, or Color"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                const bool hasPayload = parameters.value("hasPayload", false);
                BINDING::ValueType payloadType = BINDING::ValueType::Float;
                const std::string payloadTypeText = parameters.value("payloadType", std::string{});
                if (!payloadTypeText.empty() && !TryParseRuntimeBindingEditorValueType(payloadTypeText, payloadType)) {
                    return OperationFailure("UNKNOWN_VALUE_TYPE", "Unknown runtime event payload type");
                }
                std::string id{};
                if (!controller.AddEventDefinition(*scene, hasPayload, payloadType,
                                                   parameters.value("name", std::string{}),
                                                   parameters.value("id", std::string{}), id, result.message)) {
                    result.code = "CREATE_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"kind", "event"},
                    {"id", id},
                    {"workspace", "runtime_binding"},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.item.property.set",
            "Set one schema-described property on a runtime binding item.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime binding ID"),
                MakeParameter("property", EditorOperationParameterType::String, true,
                              "Property ID returned by binding.item.describe"),
                MakeParameter("value", EditorOperationParameterType::Json, true, "Typed property value"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.SetItemProperty(*scene, parameters.value("id", std::string{}),
                                                parameters.value("property", std::string{}), parameters.at("value"),
                                                result.message)) {
                    result.code = "PROPERTY_SET_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"id", parameters.value("id", std::string{})},
                    {"property", parameters.value("property", std::string{})},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.item.remove",
            "Remove a runtime binding item by stable ID.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime binding ID"),
                MakeParameter("confirm", EditorOperationParameterType::Bool, false,
                              "Explicitly allow referenced slots to become unresolved"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                const RuntimeBindingRemovalResult removal = controller.RemoveItem(
                    *scene, parameters.value("id", std::string{}), parameters.value("confirm", false));
                result.message = removal.message;
                result.data["referenceIndexAvailable"] = removal.referenceIndexAvailable;
                result.data["referenceScope"] = "current_scene_and_loaded_material_assets";
                result.data["referenceScopeComplete"] = false;
                result.data["references"] = nlohmann::json::array();
                for (const RuntimeBindingReference& reference : removal.references) {
                    result.data["references"].push_back(WriteReference(reference));
                }
                if (removal.confirmationRequired) {
                    result.code = "PENDING_CONFIRMATION";
                    result.hints = {
                        "Repeat binding.item.remove with confirm=true after user approval",
                    };
                    return result;
                }
                if (!removal.removed) {
                    result.code = "REMOVE_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.references.query",
            "List editor-authored slots referencing one runtime binding ID.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime binding ID"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                (void)scene;
                const std::string id = parameters.value("id", std::string{});
                if (controller.GetCatalog().FindVariable(id) == nullptr &&
                    controller.GetCatalog().FindEvent(id) == nullptr) {
                    return OperationFailure("ITEM_NOT_FOUND", "Runtime binding item not found: " + id);
                }
                result.data["id"] = id;
                result.data["referenceIndexAvailable"] = controller.IsReferenceIndexAvailable();
                result.data["referenceScope"] = "current_scene_and_loaded_material_assets";
                result.data["referenceScopeComplete"] = false;
                result.data["references"] = nlohmann::json::array();
                for (const RuntimeBindingReference& reference : controller.GetReferences(id)) {
                    result.data["references"].push_back(WriteReference(reference));
                }
                result.ok = true;
                result.code = "OK";
                result.message = "Runtime binding references returned";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);

    registry.Register(
        {
            "binding.preview.set_variable",
            "Set a session-only preview value through the editor writer.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime variable ID"),
                MakeParameter("value", EditorOperationParameterType::Json, true,
                              "Typed value matching the variable definition"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.PreviewSetVariable(*scene, parameters.value("id", std::string{}),
                                                   parameters.at("value"), result.message)) {
                    result.code = "PREVIEW_SET_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"sessionOnly", true},
                    {"id", parameters.value("id", std::string{})},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Session);

    registry.Register(
        {
            "binding.preview.emit_event",
            "Emit a session-only test event into the current runtime frame.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, true, "Stable runtime event ID"),
                MakeParameter("payload", EditorOperationParameterType::Json, false,
                              "Typed payload when required by the event"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                const auto payload = parameters.find("payload");
                const nlohmann::json* payloadPointer = payload != parameters.end() ? &*payload : nullptr;
                if (!controller.PreviewEmitEvent(*scene, parameters.value("id", std::string{}), payloadPointer,
                                                 result.message)) {
                    result.code = "PREVIEW_EMIT_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"sessionOnly", true},
                    {"id", parameters.value("id", std::string{})},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Session);

    registry.Register(
        {
            "binding.preview.reset",
            "Release one or all editor-owned runtime variable previews.",
            "runtime_binding",
            {
                MakeParameter("id", EditorOperationParameterType::String, false,
                              "Variable ID; omit to release every editor preview"),
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.PreviewReset(*scene, parameters.value("id", std::string{}), result.message)) {
                    result.code = "PREVIEW_RESET_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {{"sessionOnly", true}};
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Session);

    registry.Register(
        {
            "binding.save",
            "Save the project runtime binding catalog.",
            "runtime_binding",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.Save(*scene, result.message)) {
                    result.code = "SAVE_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                result.data = {
                    {"workspace", "runtime_binding"},
                };
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.undo",
            "Undo the most recent runtime binding catalog edit.",
            "runtime_binding",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.Undo(*scene, result.message)) {
                    result.code = "UNDO_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "binding.redo",
            "Redo the most recently undone runtime binding catalog edit.",
            "runtime_binding",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireContext(controller, sceneProvider, result, scene)) {
                    return result;
                }
                if (!controller.Redo(*scene, result.message)) {
                    result.code = "REDO_FAILED";
                    return result;
                }
                result.ok = true;
                result.code = "OK";
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
