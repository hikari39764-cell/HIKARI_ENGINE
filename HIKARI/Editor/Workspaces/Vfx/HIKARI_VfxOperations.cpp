#include "Editor/Workspaces/Vfx/HIKARI_VfxOperations.h"

#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#include <string>
#include <cstdint>
#include <limits>

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
        }, HIKARI::EDITOR::EditorOperationKind::Query);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.preset.list",
            "List the available Effekseer-aligned VFX starter presets",
            "vfx",
            {},
            [](const nlohmann::json&) {
                nlohmann::json presets = nlohmann::json::array();
                for (const auto& descriptor :
                    VFX::PRESETS::GetVfxPresetCatalog()) {
                    presets.push_back({
                        { "id", descriptor.id },
                        { "displayName", descriptor.displayName },
                        { "category", descriptor.category },
                        { "summary", descriptor.summary },
                    });
                }
                nlohmann::json data{};
                data["presets"] = std::move(presets);
                return Success("VFX presets listed", std::move(data));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Query, false);

        registry.Register({
            "vfx.new_from_preset",
            "Create a transient VFX document from a starter preset",
            "vfx",
            {
                Parameter(
                    "presetId",
                    EditorOperationParameterType::String,
                    true,
                    "Preset id returned by vfx.preset.list"),
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
                        "Creating a VFX preset would discard unsaved edits");
                    result.data = ContextData(controller);
                    result.hints.push_back(
                        "Repeat with confirmDiscard=true to continue");
                    return result;
                }
                std::string message{};
                if (!controller.NewAssetFromPreset(
                        parameters.at("presetId").get<std::string>(),
                        message)) {
                    return Failure("VFX_PRESET_FAILED", message);
                }
                return Success(
                    message,
                    WriteVfxWorkspaceSummary(controller));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.add_preset_layers", "Add a template to the current effect as one undoable edit", "vfx",
            {Parameter("presetId", EditorOperationParameterType::String, true, "Template ID from vfx.preset.list")},
            [&controller](const nlohmann::json& parameters) {
                std::string message;
                return controller.AddPresetLayers(parameters.at("presetId").get<std::string>(), message)
                    ? Success(message, WriteVfxWorkspaceSummary(controller))
                    : Failure("VFX_TEMPLATE_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.target.describe",
            "Inspect a VFX target; propertyPrefix narrows editable values, includeSchema controls binding metadata",
            "vfx",
            {
                Parameter(
                    "targetId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable target ID, or asset for document properties"),
                Parameter("propertyPrefix", EditorOperationParameterType::String, false,
                    "Optional property path prefix, e.g. localTransform or startDelay; omits unrelated values and serialized document"),
                Parameter("includeSchema", EditorOperationParameterType::Bool, false,
                    "Include binding schemas; defaults to false with propertyPrefix, true otherwise"),
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
                const auto prefix = parameters.value("propertyPrefix", std::string{});
                if (!parameters.value("includeSchema", prefix.empty())) description.erase("propertySchemas");
                if (!prefix.empty()) {
                    auto& properties = description["properties"];
                    nlohmann::json filtered = nlohmann::json::array();
                    for (const auto& property : properties)
                        if (property.value("id", std::string{}).starts_with(prefix)) filtered.push_back(property);
                    properties = std::move(filtered);
                    description.erase("serializedValue");
                    description["propertyPrefix"] = prefix;
                }
                description["vfxAssetGuid"] =
                    controller.GetDocument().GetAssetGuid().value;
                return Success(
                    "VFX target described",
                    std::move(description));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Query);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.burst.add",
            "Add an editable emitter-local burst key",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
                Parameter(
                    "time",
                    EditorOperationParameterType::Float,
                    true,
                    "Burst time in the emitter active window"),
                Parameter(
                    "count",
                    EditorOperationParameterType::Int,
                    true,
                    "Particles emitted by the burst key"),
                Parameter(
                    "requestedId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional stable burst key ID"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                const nlohmann::json& countNode = parameters.at("count");
                uint64_t count = 0u;
                if (countNode.is_number_integer()) {
                    const int64_t signedCount = countNode.get<int64_t>();
                    if (signedCount < 0) {
                        return Failure(
                            "VFX_BURST_ADD_FAILED",
                            "Burst count must be non-negative");
                    }
                    count = static_cast<uint64_t>(signedCount);
                } else if (countNode.is_number_unsigned()) {
                    count = countNode.get<uint64_t>();
                } else {
                    return Failure(
                        "VFX_BURST_ADD_FAILED",
                        "Burst count must be an integer");
                }
                if (count > (std::numeric_limits<uint32_t>::max)()) {
                    return Failure(
                        "VFX_BURST_ADD_FAILED",
                        "Burst count exceeds uint32 range");
                }
                std::string id{};
                std::string message{};
                if (!controller.AddBurstKey(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("time").get<float>(),
                        static_cast<uint32_t>(count),
                        parameters.value("requestedId", ""),
                        id,
                        message)) {
                    return Failure("VFX_BURST_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.burst.set",
            "Update an emitter-local burst key through one authoring transaction",
            "vfx",
            {
                Parameter(
                    "keyId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable burst key ID"),
                Parameter(
                    "time",
                    EditorOperationParameterType::Float,
                    true,
                    "Burst time in the emitter active window"),
                Parameter(
                    "count",
                    EditorOperationParameterType::Int,
                    true,
                    "Particles emitted by the burst key"),
            },
            [&controller](const nlohmann::json& parameters) {
                const nlohmann::json& countNode = parameters.at("count");
                uint64_t count = 0u;
                if (countNode.is_number_integer()) {
                    const int64_t signedCount = countNode.get<int64_t>();
                    if (signedCount < 0) {
                        return Failure(
                            "VFX_BURST_SET_FAILED",
                            "Burst count must be non-negative");
                    }
                    count = static_cast<uint64_t>(signedCount);
                } else if (countNode.is_number_unsigned()) {
                    count = countNode.get<uint64_t>();
                } else {
                    return Failure(
                        "VFX_BURST_SET_FAILED",
                        "Burst count must be an integer");
                }
                if (count > (std::numeric_limits<uint32_t>::max)()) {
                    return Failure(
                        "VFX_BURST_SET_FAILED",
                        "Burst count exceeds uint32 range");
                }
                std::string message{};
                return controller.SetBurstKey(
                        parameters.at("keyId").get<std::string>(),
                        parameters.at("time").get<float>(),
                        static_cast<uint32_t>(count),
                        message)
                    ? Success(message, ContextData(controller,
                        parameters.at("keyId").get<std::string>()))
                    : Failure("VFX_BURST_SET_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.burst.remove",
            "Remove one editable emitter-local burst key",
            "vfx",
            {
                Parameter(
                    "keyId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable burst key ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveBurstKey(
                        parameters.at("keyId").get<std::string>(),
                        message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_BURST_REMOVE_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.special_output.add",
            "Add a typed VFX special output profile",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
                Parameter(
                    "type",
                    EditorOperationParameterType::String,
                    true,
                    "WeaponPathDistortion, ScreenImpulse, or SkinnedAfterimage"),
                Parameter(
                    "requestedId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional stable output ID"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                VFX::VfxSpecialOutputType type{};
                if (!VFX::TryParseVfxSpecialOutputType(
                        parameters.at("type").get<std::string>(), type)) {
                    return Failure(
                        "VFX_SPECIAL_OUTPUT_ADD_FAILED",
                        "Unknown VFX special output type");
                }
                std::string id{};
                std::string message{};
                if (!controller.AddSpecialOutput(
                        parameters.at("emitterId").get<std::string>(),
                        type,
                        parameters.value("requestedId", ""),
                        id,
                        message)) {
                    return Failure(
                        "VFX_SPECIAL_OUTPUT_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.special_output.set",
            "Set a typed special output profile field through the canonical transaction",
            "vfx",
            {
                Parameter(
                    "outputId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable special output ID"),
                Parameter(
                    "property",
                    EditorOperationParameterType::String,
                    true,
                    "Profile property path returned by target.describe"),
                Parameter(
                    "value",
                    EditorOperationParameterType::Json,
                    true,
                    "Typed profile value"),
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(
                    databaseProvider, result);
                if (database == nullptr ||
                    !EnsureDocument(controller, *database, result)) {
                    return result;
                }
                const std::string outputId =
                    parameters.at("outputId").get<std::string>();
                if (controller.FindSpecialOutput(outputId) == nullptr) {
                    return Failure(
                        "VFX_SPECIAL_OUTPUT_NOT_FOUND",
                        "VFX special output was not found");
                }
                std::string message{};
                if (!controller.SetProperty(
                        outputId,
                        parameters.at("property").get<std::string>(),
                        parameters.at("value"),
                        message,
                        0u,
                        database)) {
                    return Failure("VFX_SPECIAL_OUTPUT_REJECTED", message);
                }
                nlohmann::json description{};
                (void)DescribeVfxTarget(controller, outputId, description);
                return Success(message, std::move(description));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.special_output.remove",
            "Remove a typed VFX special output profile",
            "vfx",
            {
                Parameter(
                    "outputId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable special output ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveSpecialOutput(
                        parameters.at("outputId").get<std::string>(),
                        message)
                    ? Success(message, ContextData(controller))
                    : Failure(
                        "VFX_SPECIAL_OUTPUT_REMOVE_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.emitter.add",
            "Add a Shape, Particles, Group or Adjustment layer to the composition",
            "vfx",
            {
                Parameter(
                    "parentEmitterId",
                    EditorOperationParameterType::String,
                    false,
                    "Optional stable parent emitter ID"),
                Parameter("kind", EditorOperationParameterType::String, false,
                    "Shape (default), Particles, Group, or Adjustment. Adjustments affect lower siblings and their descendants only. Mapping supports Group3D deformation (default) or LayerUv; styles process the lower-layer rendered composition and exclude the scene background."),
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
                const auto kindName = parameters.value("kind", "Shape");
                if (kindName != "Shape" && kindName != "Particles" && kindName != "Group" && kindName != "Adjustment")
                    return Failure("VFX_INVALID_LAYER_KIND", "Expected Shape, Particles, Group or Adjustment");
                const auto kind = kindName == "Shape" ? VFX::VfxLayerKind::Shape :
                    kindName == "Group" ? VFX::VfxLayerKind::Group :
                    kindName == "Adjustment" ? VFX::VfxLayerKind::Adjustment : VFX::VfxLayerKind::Particles;
                if (!controller.AddEmitter(
                        parameters.value("parentEmitterId", ""),
                        parameters.value("name", ""),
                        parameters.value("requestedId", ""),
                        id,
                        message, kind)) {
                    return Failure("VFX_EMITTER_ADD_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.emitter.duplicate",
            "Duplicate a complete layer subtree with new stable IDs, preserving child hierarchy and animation",
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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

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
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.emitter.reparent",
            "Move an emitter under a new parent (or to root with empty parent)",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID to move"),
                Parameter(
                    "parentEmitterId",
                    EditorOperationParameterType::String,
                    false,
                    "New parent emitter ID, empty for root"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                const std::string emitterId =
                    parameters.at("emitterId").get<std::string>();
                return controller.ReparentEmitter(
                        emitterId,
                        parameters.value("parentEmitterId", ""),
                        message)
                    ? Success(message, ContextData(controller, emitterId))
                    : Failure("VFX_EMITTER_REPARENT_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.emitter.set_hidden",
            "Hide or show an emitter in the preview (editor session state)",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable emitter ID"),
                Parameter(
                    "hidden",
                    EditorOperationParameterType::Bool,
                    true,
                    "True hides from preview, false shows"),
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string emitterId =
                    parameters.at("emitterId").get<std::string>();
                if (controller.FindEmitter(emitterId) == nullptr) {
                    return Failure(
                        "VFX_EMITTER_NOT_FOUND",
                        "VFX emitter was not found");
                }
                controller.SetEmitterHidden(
                    emitterId,
                    parameters.at("hidden").get<bool>());
                return Success(
                    "VFX emitter preview visibility updated",
                    ContextData(controller, emitterId));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.emitter.set_solo",
            "Solo one emitter in the preview, or clear solo with empty ID",
            "vfx",
            {
                Parameter(
                    "emitterId",
                    EditorOperationParameterType::String,
                    false,
                    "Stable emitter ID to solo, empty clears solo"),
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string emitterId =
                    parameters.value("emitterId", "");
                if (emitterId.empty()) {
                    controller.ClearSoloEmitter();
                    return Success(
                        "VFX preview solo cleared",
                        ContextData(controller));
                }
                if (controller.FindEmitter(emitterId) == nullptr) {
                    return Failure(
                        "VFX_EMITTER_NOT_FOUND",
                        "VFX emitter was not found");
                }
                controller.SetSoloEmitter(emitterId);
                return Success(
                    "VFX preview solo set",
                    ContextData(controller, emitterId));
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);

        registry.Register({
            "vfx.preview.focus_selected",
            "Focus the preview camera on the selected emitter",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                std::string message{};
                return controller.FocusSelectedEmitter(message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_FOCUS_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Session);

        registry.Register({
            "vfx.preview.step",
            "Pause the preview and advance a fixed number of frames",
            "vfx",
            {
                Parameter(
                    "frameCount",
                    EditorOperationParameterType::Int,
                    false,
                    "Frames to advance (1-120, default 1)"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                const int frames = parameters.value("frameCount", 1);
                return controller.StepPreviewFrame(frames, message)
                    ? Success(message, ContextData(controller))
                    : Failure("VFX_STEP_FAILED", message);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Session);

        RegisterVfxBindingOperations(registry, controller);
        RegisterVfxCurveOperations(registry, controller);
        RegisterVfxPreviewOperations(registry, controller);
        RegisterVfxLibraryOperations(registry, controller, databaseProvider);
    }

} // namespace HIKARI::EDITOR
