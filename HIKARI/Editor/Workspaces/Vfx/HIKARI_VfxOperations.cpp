#include "Editor/Workspaces/Vfx/HIKARI_VfxOperations.h"

#include <string>
#include <string>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSerialization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

    void RegisterVfxOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller,
        std::function<AssetDatabase*()> databaseProvider) {

        using VFXOPERATIONS::ContextData;
        using VFXOPERATIONS::EnsureDocument;
        using VFXOPERATIONS::Failure;
        using VFXOPERATIONS::Parameter;
        using VFXOPERATIONS::RequireDatabase;
        using VFXOPERATIONS::Success;

        registry.Register({
            "vfx.query",
            "Describe the open VFX document, emitters, inputs, and preview state",
            "vfx",
            {},
            [&controller, databaseProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                nlohmann::json data = WriteVfxWorkspaceSummary(controller);
                data["diagnostics"] = WriteVfxDiagnostics(controller);
                return Success("VFX workspace described", std::move(data));
            },
        });

        registry.Register({
            "vfx.new_asset",
            "Create a transient native VFX document",
            "vfx",
            {
                Parameter(
                    "name",
                    EditorOperationParameterType::String,
                    false,
                    "Display name for the new VFX asset"),
                Parameter(
                    "confirmDiscard",
                    EditorOperationParameterType::Bool,
                    false,
                    "Confirm discarding unsaved VFX edits"),
            },
            [&controller](const nlohmann::json& parameters) {
                const bool confirmDiscard = parameters.value(
                    "confirmDiscard", false);
                if (controller.GetDocument().IsDirty() &&
                    !confirmDiscard) {
                    EditorOperationResult result = Failure(
                        "PENDING_CONFIRMATION",
                        "Creating a VFX document would discard unsaved edits");
                    result.data = ContextData(controller);
                    result.hints.push_back(
                        "Repeat with confirmDiscard=true to continue");
                    return result;
                }
                controller.NewAsset(parameters.value("name", "New VFX"));
                return Success(
                    "New VFX document created",
                    WriteVfxWorkspaceSummary(controller));
            },
        });

        registry.Register({
            "vfx.open_asset",
            "Open a VFX source asset by GUID",
            "vfx",
            {
                Parameter(
                    "assetGuid",
                    EditorOperationParameterType::String,
                    true,
                    "Stable VFX asset GUID"),
                Parameter(
                    "confirmDiscard",
                    EditorOperationParameterType::Bool,
                    false,
                    "Confirm discarding unsaved VFX edits"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                const AssetGuid guid{
                    parameters.at("assetGuid").get<std::string>()
                };
                const bool confirmDiscard = parameters.value(
                    "confirmDiscard", false);
                if (controller.GetDocument().IsDirty() &&
                    controller.GetDocument().GetAssetGuid() != guid &&
                    !confirmDiscard) {
                    result = Failure(
                        "PENDING_CONFIRMATION",
                        "Opening another VFX asset would discard unsaved edits");
                    result.data = ContextData(controller);
                    result.hints.push_back(
                        "Repeat with confirmDiscard=true to continue");
                    return result;
                }
                std::string message{};
                if (!controller.OpenAsset(
                        *database, guid, message, confirmDiscard)) {
                    return Failure("VFX_OPEN_FAILED", message);
                }
                return Success(
                    message,
                    WriteVfxWorkspaceSummary(controller));
            },
        });

        registry.Register({
            "vfx.save",
            "Save and import the open VFX source asset",
            "vfx",
            {},
            [&controller, databaseProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                std::string message{};
                if (!controller.Save(*database, message)) {
                    return Failure("VFX_SAVE_FAILED", message);
                }
                return Success(
                    message,
                    WriteVfxWorkspaceSummary(controller));
            },
        });

        registry.Register({
            "vfx.undo",
            "Undo the last VFX authoring transaction",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                std::string message{};
                return controller.Undo(message)
                    ? Success(message, ContextData(controller))
                    : Failure("NOTHING_TO_UNDO", message);
            },
        });

        registry.Register({
            "vfx.redo",
            "Redo the next VFX authoring transaction",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                std::string message{};
                return controller.Redo(message)
                    ? Success(message, ContextData(controller))
                    : Failure("NOTHING_TO_REDO", message);
            },
        });

        registry.Register({
            "vfx.target.describe",
            "Describe one stable VFX asset, emitter, module, input, link, key, or stop",
            "vfx",
            {
                Parameter(
                    "targetId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable target ID, or asset for document properties"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                const std::string targetId =
                    parameters.at("targetId").get<std::string>();
                nlohmann::json description{};
                if (!DescribeVfxTarget(
                        controller, targetId, description)) {
                    return Failure(
                        "VFX_TARGET_NOT_FOUND",
                        "VFX target was not found: " + targetId);
                }
                description["workspace"] = "vfx";
                description["vfxAssetGuid"] =
                    controller.GetDocument().GetAssetGuid().value;
                return Success(
                    "VFX target described",
                    std::move(description));
            },
        });

        registry.Register({
            "vfx.property.set",
            "Set a property on a stable VFX target",
            "vfx",
            {
                Parameter(
                    "targetId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable target ID"),
                Parameter(
                    "property",
                    EditorOperationParameterType::String,
                    true,
                    "Property ID returned by vfx.target.describe"),
                Parameter(
                    "value",
                    EditorOperationParameterType::Json,
                    true,
                    "Typed property value"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                const std::string targetId =
                    parameters.at("targetId").get<std::string>();
                std::string message{};
                if (!controller.SetProperty(
                        targetId,
                        parameters.at("property").get<std::string>(),
                        parameters.at("value"),
                        message,
                        0u,
                        database)) {
                    return Failure(
                        message.starts_with("DOMAIN_MISMATCH:")
                            ? "DOMAIN_MISMATCH"
                            : "VFX_PROPERTY_REJECTED",
                        message);
                }
                nlohmann::json description{};
                (void)DescribeVfxTarget(
                    controller, targetId, description);
                description["workspace"] = "vfx";
                description["vfxAssetGuid"] =
                    controller.GetDocument().GetAssetGuid().value;
                return Success(message, std::move(description));
            },
        });

        registry.Register({
            "vfx.emitter.add",
            "Add a root or child emitter to the VFX stack",
            "vfx",
            {
                Parameter(
                    "parentEmitterId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional stable parent emitter ID"),
                Parameter(
                    "name",
                    EditorOperationParameterType::String,
                    false,
                    "Emitter display name"),
                Parameter(
                    "requestedId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional caller-provided stable ID"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                std::string id{};
                std::string message{};
                if (!controller.AddEmitter(
                        parameters.value("parentEmitterId", ""),
                        parameters.value("name", ""),
                        parameters.value("requestedId", ""),
                        id,
                        message)) {
                    return Failure("VFX_EMITTER_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.emitter.duplicate",
            "Duplicate one emitter with new stable IDs",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.DuplicateEmitter(
                        parameters.at("emitterId").get<std::string>(),
                        id,
                        message)) {
                    return Failure(
                        "VFX_EMITTER_DUPLICATE_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.emitter.remove",
            "Remove one emitter and promote its children",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveEmitter(
                        parameters.at("emitterId").get<std::string>(),
                        message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_EMITTER_REMOVE_FAILED", message);
            },
        });

        RegisterVfxBindingOperations(registry, controller);
        RegisterVfxCurveOperations(registry, controller);
        RegisterVfxPreviewOperations(registry, controller);
    }

} // namespace HIKARI::EDITOR
