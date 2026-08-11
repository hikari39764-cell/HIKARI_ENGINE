#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

    void RegisterVfxBindingOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller) {

        using VFXOPERATIONS::ContextData;
        using VFXOPERATIONS::Failure;
        using VFXOPERATIONS::Parameter;
        using VFXOPERATIONS::Success;

        registry.Register({
            "vfx.input.add",
            "Add an exposed VFX input",
            "vfx",
            {
                Parameter(
                    "type",
                    EditorOperationParameterType::String,
                    true,
                    "Bool, Int, Float, Float2, Float3, Float4, or Color"),
                Parameter(
                    "name",
                    EditorOperationParameterType::String,
                    false,
                    "Input display name"),
                Parameter(
                    "requestedId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional caller-provided stable ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                VFX::VfxValueType type{};
                if (!VFX::TryParseVfxValueType(
                        parameters.at("type").get<std::string>(), type)) {
                    return Failure(
                        "VFX_INPUT_TYPE_INVALID",
                        "Unknown VFX input type");
                }
                std::string id{};
                std::string message{};
                if (!controller.AddInput(
                        type,
                        parameters.value("name", ""),
                        parameters.value("requestedId", ""),
                        id,
                        message)) {
                    return Failure("VFX_INPUT_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.input.remove",
            "Remove an exposed input and its dependent links",
            "vfx",
            {
                Parameter(
                    "inputId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable exposed input ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveInput(
                        parameters.at("inputId").get<std::string>(),
                        message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_INPUT_REMOVE_FAILED", message);
            },
        });

        registry.Register({
            "vfx.link.add",
            "Bind an exposed input to an emitter module target",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
                Parameter(
                    "inputId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable exposed input ID"),
                Parameter(
                    "moduleId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable module ID returned by vfx.target.describe"),
                Parameter(
                    "propertyId",
                    EditorOperationParameterType::String,
                    true,
                    "Runtime-bindable property ID from the VFX property schema"),
                Parameter(
                    "requestedId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional caller-provided stable ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddInputLink(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("inputId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(),
                        parameters.value("requestedId", ""),
                        id,
                        message)) {
                    return Failure("VFX_LINK_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.link.remove",
            "Remove a VFX input link",
            "vfx",
            {
                Parameter(
                    "linkId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable input link ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveInputLink(
                        parameters.at("linkId").get<std::string>(),
                        message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_LINK_REMOVE_FAILED", message);
            },
        });

    }

} // namespace HIKARI::EDITOR
