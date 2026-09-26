#include "Editor/Workspaces/Vfx/HIKARI_VfxOperations.h"

#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

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

namespace {
using VFXOPERATIONS::ContextData;
using VFXOPERATIONS::PendingDiscard;
using VFXOPERATIONS::RequireDatabase;
using VFXOPERATIONS::RequireDocument;
using DatabaseProvider = std::function<AssetDatabase*()>;

// 非負で uint32 に収まる整数だけを受け付ける。
bool ReadBurstCount(const nlohmann::json& node, uint32_t& outCount, std::string& outError) {
    uint64_t count = 0u;
    if (node.is_number_unsigned()) {
        count = node.get<uint64_t>();
    } else if (node.is_number_integer()) {
        const int64_t signedCount = node.get<int64_t>();
        if (signedCount < 0) {
            outError = "Burst count must be non-negative";
            return false;
        }
        count = static_cast<uint64_t>(signedCount);
    } else {
        outError = "Burst count must be an integer";
        return false;
    }
    if (count > (std::numeric_limits<uint32_t>::max)()) {
        outError = "Burst count exceeds uint32 range";
        return false;
    }
    outCount = static_cast<uint32_t>(count);
    return true;
}

void RegisterDocumentOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                                const DatabaseProvider& databaseProvider) {
    registry.Register(
        {
            "vfx.query",
            "Describe the open VFX document, emitters, inputs, and preview state",
            "vfx",
            {},
            [&controller, databaseProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                nlohmann::json data = WriteVfxWorkspaceSummary(controller);
                data["diagnostics"] = WriteVfxDiagnostics(controller);
                return OperationOk("VFX workspace described", std::move(data));
            },
        },
        EditorOperationKind::Query);

    registry.Register(
        {
            "vfx.new_asset",
            "Create a transient native VFX document",
            "vfx",
            {
                {"name", EditorOperationParameterType::String, false, "Display name for the new VFX asset"},
                {"confirmDiscard", EditorOperationParameterType::Bool, false, "Confirm discarding unsaved VFX edits"},
            },
            [&controller](const nlohmann::json& parameters) {
                const bool confirmDiscard = parameters.value("confirmDiscard", false);
                if (controller.GetDocument().IsDirty() && !confirmDiscard) {
                    return PendingDiscard(controller, "Creating a VFX document would discard unsaved edits");
                }
                controller.NewAsset(parameters.value("name", "New VFX"));
                return OperationOk("New VFX document created", WriteVfxWorkspaceSummary(controller));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.preset.list",
            "List the available Effekseer-aligned VFX starter presets",
            "vfx",
            {},
            [](const nlohmann::json&) {
                nlohmann::json presets = nlohmann::json::array();
                for (const auto& descriptor : VFX::PRESETS::GetVfxPresetCatalog()) {
                    presets.push_back({
                        {"id", descriptor.id},
                        {"displayName", descriptor.displayName},
                        {"category", descriptor.category},
                        {"summary", descriptor.summary},
                    });
                }
                nlohmann::json data{};
                data["presets"] = std::move(presets);
                return OperationOk("VFX presets listed", std::move(data));
            },
        },
        EditorOperationKind::Query, false);

    registry.Register(
        {
            "vfx.new_from_preset",
            "Create a transient VFX document from a starter preset",
            "vfx",
            {
                {"presetId", EditorOperationParameterType::String, true, "Preset id returned by vfx.preset.list"},
                {"confirmDiscard", EditorOperationParameterType::Bool, false, "Confirm discarding unsaved VFX edits"},
            },
            [&controller](const nlohmann::json& parameters) {
                const bool confirmDiscard = parameters.value("confirmDiscard", false);
                if (controller.GetDocument().IsDirty() && !confirmDiscard) {
                    return PendingDiscard(controller, "Creating a VFX preset would discard unsaved edits");
                }
                std::string message{};
                if (!controller.NewAssetFromPreset(parameters.at("presetId").get<std::string>(), message)) {
                    return OperationFailure("VFX_PRESET_FAILED", message);
                }
                return OperationOk(message, WriteVfxWorkspaceSummary(controller));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.add_preset_layers",
            "Add a template to the current effect as one undoable edit",
            "vfx",
            {{"presetId", EditorOperationParameterType::String, true, "Template ID from vfx.preset.list"}},
            [&controller](const nlohmann::json& parameters) {
                std::string message;
                return controller.AddPresetLayers(parameters.at("presetId").get<std::string>(), message)
                           ? OperationOk(message, WriteVfxWorkspaceSummary(controller))
                           : OperationFailure("VFX_TEMPLATE_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.open_asset",
            "Open a VFX source asset by GUID",
            "vfx",
            {
                {"assetGuid", EditorOperationParameterType::String, true, "Stable VFX asset GUID"},
                {"confirmDiscard", EditorOperationParameterType::Bool, false, "Confirm discarding unsaved VFX edits"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDatabase(databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                const AssetGuid guid{parameters.at("assetGuid").get<std::string>()};
                const bool confirmDiscard = parameters.value("confirmDiscard", false);
                if (controller.GetDocument().IsDirty() && controller.GetDocument().GetAssetGuid() != guid &&
                    !confirmDiscard) {
                    return PendingDiscard(controller, "Opening another VFX asset would discard unsaved edits");
                }
                std::string message{};
                if (!controller.OpenAsset(*database, guid, message, confirmDiscard)) {
                    return OperationFailure("VFX_OPEN_FAILED", message);
                }
                return OperationOk(message, WriteVfxWorkspaceSummary(controller));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.save",
            "Save and import the open VFX source asset",
            "vfx",
            {},
            [&controller, databaseProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                std::string message{};
                if (!controller.Save(*database, message)) {
                    return OperationFailure("VFX_SAVE_FAILED", message);
                }
                return OperationOk(message, WriteVfxWorkspaceSummary(controller));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.undo",
            "Undo the last VFX authoring transaction",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                std::string message{};
                return controller.Undo(message) ? OperationOk(message, ContextData(controller))
                                                : OperationFailure("NOTHING_TO_UNDO", message);
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.redo",
            "Redo the next VFX authoring transaction",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                std::string message{};
                return controller.Redo(message) ? OperationOk(message, ContextData(controller))
                                                : OperationFailure("NOTHING_TO_REDO", message);
            },
        },
        EditorOperationKind::Mutation);
}

void RegisterTargetOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                              const DatabaseProvider& databaseProvider) {
    registry.Register(
        {
            "vfx.target.describe",
            "Inspect a VFX target; propertyPrefix narrows editable values, includeSchema controls binding metadata",
            "vfx",
            {
                {"targetId", EditorOperationParameterType::String, true,
                 "Stable target ID, or asset for document properties"},
                {"propertyPrefix", EditorOperationParameterType::String, false,
                 "Optional property path prefix, e.g. localTransform or startDelay; omits unrelated values and serialized document"},
                {"includeSchema", EditorOperationParameterType::Bool, false,
                 "Include binding schemas; defaults to false with propertyPrefix, true otherwise"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                const std::string targetId = parameters.at("targetId").get<std::string>();
                nlohmann::json description{};
                if (!DescribeVfxTarget(controller, targetId, description)) {
                    return OperationFailure("VFX_TARGET_NOT_FOUND", "VFX target was not found: " + targetId);
                }
                description["workspace"] = "vfx";
                const auto prefix = parameters.value("propertyPrefix", std::string{});
                if (!parameters.value("includeSchema", prefix.empty()))
                    description.erase("propertySchemas");
                if (!prefix.empty()) {
                    auto& properties = description["properties"];
                    nlohmann::json filtered = nlohmann::json::array();
                    for (const auto& property : properties)
                        if (property.value("id", std::string{}).starts_with(prefix))
                            filtered.push_back(property);
                    properties = std::move(filtered);
                    description.erase("serializedValue");
                    description["propertyPrefix"] = prefix;
                }
                description["vfxAssetGuid"] = controller.GetDocument().GetAssetGuid().value;
                return OperationOk("VFX target described", std::move(description));
            },
        },
        EditorOperationKind::Query);

    registry.Register(
        {
            "vfx.property.set",
            "Set a property on a stable VFX target",
            "vfx",
            {
                {"targetId", EditorOperationParameterType::String, true, "Stable target ID"},
                {"property", EditorOperationParameterType::String, true, "Property ID returned by vfx.target.describe"},
                {"value", EditorOperationParameterType::Json, true, "Typed property value"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                const std::string targetId = parameters.at("targetId").get<std::string>();
                std::string message{};
                if (!controller.SetProperty(targetId, parameters.at("property").get<std::string>(),
                                            parameters.at("value"), message, 0u, database)) {
                    return OperationFailure(
                        message.starts_with("DOMAIN_MISMATCH:") ? "DOMAIN_MISMATCH" : "VFX_PROPERTY_REJECTED", message);
                }
                nlohmann::json description{};
                (void)DescribeVfxTarget(controller, targetId, description);
                description["workspace"] = "vfx";
                description["vfxAssetGuid"] = controller.GetDocument().GetAssetGuid().value;
                return OperationOk(message, std::move(description));
            },
        },
        EditorOperationKind::Mutation);
}

void RegisterBurstOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                             const DatabaseProvider& databaseProvider) {
    registry.Register(
        {
            "vfx.burst.add",
            "Add an editable emitter-local burst key",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"time", EditorOperationParameterType::Float, true, "Burst time in the emitter active window"},
                {"count", EditorOperationParameterType::Int, true, "Particles emitted by the burst key"},
                {"requestedId", EditorOperationParameterType::String, false, "Optional stable burst key ID"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                uint32_t count = 0u;
                std::string message{};
                if (!ReadBurstCount(parameters.at("count"), count, message)) {
                    return OperationFailure("VFX_BURST_ADD_FAILED", message);
                }
                std::string id{};
                if (!controller.AddBurstKey(parameters.at("emitterId").get<std::string>(),
                                            parameters.at("time").get<float>(), count,
                                            parameters.value("requestedId", ""), id, message)) {
                    return OperationFailure("VFX_BURST_ADD_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.burst.set",
            "Update an emitter-local burst key through one authoring transaction",
            "vfx",
            {
                {"keyId", EditorOperationParameterType::String, true, "Stable burst key ID"},
                {"time", EditorOperationParameterType::Float, true, "Burst time in the emitter active window"},
                {"count", EditorOperationParameterType::Int, true, "Particles emitted by the burst key"},
            },
            [&controller](const nlohmann::json& parameters) {
                uint32_t count = 0u;
                std::string message{};
                if (!ReadBurstCount(parameters.at("count"), count, message)) {
                    return OperationFailure("VFX_BURST_SET_FAILED", message);
                }
                return controller.SetBurstKey(parameters.at("keyId").get<std::string>(),
                                              parameters.at("time").get<float>(), count, message)
                           ? OperationOk(message, ContextData(controller, parameters.at("keyId").get<std::string>()))
                           : OperationFailure("VFX_BURST_SET_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.burst.remove",
            "Remove one editable emitter-local burst key",
            "vfx",
            {
                {"keyId", EditorOperationParameterType::String, true, "Stable burst key ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveBurstKey(parameters.at("keyId").get<std::string>(), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_BURST_REMOVE_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);
}

void RegisterSpecialOutputOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                                     const DatabaseProvider& databaseProvider) {
    registry.Register(
        {
            "vfx.special_output.add",
            "Add a typed VFX special output profile",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"type", EditorOperationParameterType::String, true,
                 "WeaponPathDistortion, ScreenImpulse, or SkinnedAfterimage"},
                {"requestedId", EditorOperationParameterType::String, false, "Optional stable output ID"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                VFX::VfxSpecialOutputType type{};
                if (!TEXT::TryParseEnum(VFX::kVfxSpecialOutputTypeNames, parameters.at("type").get<std::string>(),
                                        type)) {
                    return OperationFailure("VFX_SPECIAL_OUTPUT_ADD_FAILED", "Unknown VFX special output type");
                }
                std::string id{};
                std::string message{};
                if (!controller.AddSpecialOutput(parameters.at("emitterId").get<std::string>(), type,
                                                 parameters.value("requestedId", ""), id, message)) {
                    return OperationFailure("VFX_SPECIAL_OUTPUT_ADD_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.special_output.set",
            "Set a typed special output profile field through the canonical transaction",
            "vfx",
            {
                {"outputId", EditorOperationParameterType::String, true, "Stable special output ID"},
                {"property", EditorOperationParameterType::String, true,
                 "Profile property path returned by target.describe"},
                {"value", EditorOperationParameterType::Json, true, "Typed profile value"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                const std::string outputId = parameters.at("outputId").get<std::string>();
                if (controller.FindSpecialOutput(outputId) == nullptr) {
                    return OperationFailure("VFX_SPECIAL_OUTPUT_NOT_FOUND", "VFX special output was not found");
                }
                std::string message{};
                if (!controller.SetProperty(outputId, parameters.at("property").get<std::string>(),
                                            parameters.at("value"), message, 0u, database)) {
                    return OperationFailure("VFX_SPECIAL_OUTPUT_REJECTED", message);
                }
                nlohmann::json description{};
                (void)DescribeVfxTarget(controller, outputId, description);
                return OperationOk(message, std::move(description));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.special_output.remove",
            "Remove a typed VFX special output profile",
            "vfx",
            {
                {"outputId", EditorOperationParameterType::String, true, "Stable special output ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveSpecialOutput(parameters.at("outputId").get<std::string>(), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_SPECIAL_OUTPUT_REMOVE_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);
}

void RegisterEmitterOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                               const DatabaseProvider& databaseProvider) {
    registry.Register(
        {
            "vfx.emitter.add",
            "Add a Shape, Particles, Group or Adjustment layer to the composition",
            "vfx",
            {
                {"parentEmitterId", EditorOperationParameterType::String, false, "Optional stable parent emitter ID"},
                {"kind", EditorOperationParameterType::String, false,
                 "Shape (default), Particles, Group, or Adjustment. Adjustments affect lower siblings and their descendants only. Mapping supports Group3D deformation (default) or LayerUv; styles process the lower-layer rendered composition and exclude the scene background."},
                {"name", EditorOperationParameterType::String, false, "Emitter display name"},
                {"requestedId", EditorOperationParameterType::String, false, "Optional caller-provided stable ID"},
            },
            [&controller, databaseProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                AssetDatabase* database = RequireDocument(controller, databaseProvider, result);
                if (database == nullptr) {
                    return result;
                }
                std::string id{};
                std::string message{};
                VFX::VfxLayerKind kind = VFX::VfxLayerKind::Shape;
                if (!TEXT::TryParseEnum(VFX::kVfxLayerKindNames, parameters.value("kind", "Shape"), kind))
                    return OperationFailure("VFX_INVALID_LAYER_KIND", "Expected Shape, Particles, Group or Adjustment");
                if (!controller.AddEmitter(parameters.value("parentEmitterId", ""), parameters.value("name", ""),
                                           parameters.value("requestedId", ""), id, message, kind)) {
                    return OperationFailure("VFX_EMITTER_ADD_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.emitter.duplicate",
            "Duplicate a complete layer subtree with new stable IDs, preserving child hierarchy and animation",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.DuplicateEmitter(parameters.at("emitterId").get<std::string>(), id, message)) {
                    return OperationFailure("VFX_EMITTER_DUPLICATE_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.emitter.remove",
            "Remove one emitter and promote its children",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.RemoveEmitter(parameters.at("emitterId").get<std::string>(), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_EMITTER_REMOVE_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.emitter.reparent",
            "Move an emitter under a new parent (or to root with empty parent)",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID to move"},
                {"parentEmitterId", EditorOperationParameterType::String, false,
                 "New parent emitter ID, empty for root"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                const std::string emitterId = parameters.at("emitterId").get<std::string>();
                return controller.ReparentEmitter(emitterId, parameters.value("parentEmitterId", ""), message)
                           ? OperationOk(message, ContextData(controller, emitterId))
                           : OperationFailure("VFX_EMITTER_REPARENT_FAILED", message);
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.emitter.set_hidden",
            "Hide or show an emitter in the preview (editor session state)",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"hidden", EditorOperationParameterType::Bool, true, "True hides from preview, false shows"},
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string emitterId = parameters.at("emitterId").get<std::string>();
                if (controller.FindEmitter(emitterId) == nullptr) {
                    return OperationFailure("VFX_EMITTER_NOT_FOUND", "VFX emitter was not found");
                }
                controller.Preview().SetEmitterHidden(emitterId, parameters.at("hidden").get<bool>());
                return OperationOk("VFX emitter preview visibility updated", ContextData(controller, emitterId));
            },
        },
        EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.emitter.set_solo",
            "Solo one emitter in the preview, or clear solo with empty ID",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, false,
                 "Stable emitter ID to solo, empty clears solo"},
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string emitterId = parameters.value("emitterId", "");
                if (emitterId.empty()) {
                    controller.Preview().ClearSoloEmitter();
                    return OperationOk("VFX preview solo cleared", ContextData(controller));
                }
                if (controller.FindEmitter(emitterId) == nullptr) {
                    return OperationFailure("VFX_EMITTER_NOT_FOUND", "VFX emitter was not found");
                }
                controller.Preview().SetSoloEmitter(emitterId);
                return OperationOk("VFX preview solo set", ContextData(controller, emitterId));
            },
        },
        EditorOperationKind::Mutation);
}

} // namespace

void RegisterVfxOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                           std::function<AssetDatabase*()> databaseProvider) {
    RegisterDocumentOperations(registry, controller, databaseProvider);
    RegisterTargetOperations(registry, controller, databaseProvider);
    RegisterBurstOperations(registry, controller, databaseProvider);
    RegisterSpecialOutputOperations(registry, controller, databaseProvider);
    RegisterEmitterOperations(registry, controller, databaseProvider);
    RegisterVfxBindingOperations(registry, controller);
    RegisterVfxCurveOperations(registry, controller);
    RegisterVfxPreviewOperations(registry, controller);
    RegisterVfxLibraryOperations(registry, controller, databaseProvider);
}

} // namespace HIKARI::EDITOR
