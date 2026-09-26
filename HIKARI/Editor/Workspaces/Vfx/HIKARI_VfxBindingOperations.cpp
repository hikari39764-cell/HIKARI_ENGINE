#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

void RegisterVfxBindingOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller) {
    using VFXOPERATIONS::ContextData;

    registry.Register(
        {
            "vfx.input.add",
            "Add an exposed VFX input",
            "vfx",
            {
                {"type", EditorOperationParameterType::String, true,
                 "Bool, Int, Float, Float2, Float3, Float4, or Color"},
                {"name", EditorOperationParameterType::String, false, "Input display name"},
                {"requestedId", EditorOperationParameterType::String, false, "Optional caller-provided stable ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                VFX::VfxValueType type{};
                if (!TEXT::TryParseEnum(VFX::kVfxValueTypeNames, parameters.at("type").get<std::string>(), type)) {
                    return OperationFailure("VFX_INPUT_TYPE_INVALID", "Unknown VFX input type");
                }
                std::string id{};
                std::string message{};
                if (!controller.AddInput(type, parameters.value("name", ""), parameters.value("requestedId", ""), id,
                                         message)) {
                    return OperationFailure("VFX_INPUT_ADD_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.input.remove",
            "Remove an exposed input and its dependent links",
            "vfx",
            {
                {"inputId", EditorOperationParameterType::String, true, "Stable exposed input ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveInput(parameters.at("inputId").get<std::string>(), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_INPUT_REMOVE_FAILED", message);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.link.add",
            "Bind an exposed input to an emitter module target",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"inputId", EditorOperationParameterType::String, true, "Stable exposed input ID"},
                {"moduleId", EditorOperationParameterType::String, true,
                 "Stable module ID returned by vfx.target.describe"},
                {"propertyId", EditorOperationParameterType::String, true,
                 "Runtime-bindable property ID from the VFX property schema"},
                {"requestedId", EditorOperationParameterType::String, false, "Optional caller-provided stable ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddInputLink(
                        parameters.at("emitterId").get<std::string>(), parameters.at("inputId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(), parameters.at("propertyId").get<std::string>(),
                        parameters.value("requestedId", ""), id, message)) {
                    return OperationFailure("VFX_LINK_ADD_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.link.remove",
            "Remove a VFX input link",
            "vfx",
            {
                {"linkId", EditorOperationParameterType::String, true, "Stable input link ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveInputLink(parameters.at("linkId").get<std::string>(), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_LINK_REMOVE_FAILED", message);
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
