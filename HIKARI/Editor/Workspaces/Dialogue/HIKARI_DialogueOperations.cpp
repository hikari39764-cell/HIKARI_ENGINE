#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueOperations.h"

#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {
bool RequireDialogueScene(const std::function<DocumentSceneBase*()>& provider, EditorOperationResult& result,
                          DocumentSceneBase*& outScene) {
    outScene = provider ? provider() : nullptr;
    if (outScene != nullptr)
        return true;
    result.code = "NO_ACTIVE_SCENE";
    result.message = "No active scene owns the dialogue workspace.";
    return false;
}

bool RequireDocument(DialogueWorkspaceController& controller, EditorOperationResult& result) {
    if (controller.IsDocumentOpen())
        return true;
    result.code = "NO_DIALOGUE_DOCUMENT";
    result.message = "Open or create a dialogue document first.";
    return false;
}

EditorOperationResult PendingConfirmation(std::string message) {
    EditorOperationResult result{};
    result.code = "PENDING_CONFIRMATION";
    result.message = std::move(message);
    result.hints = {
        "Use dialogue.save before retrying, or answer the visible "
        "Save / Discard / Cancel confirmation in the editor.",
    };
    return result;
}

void RevealDialogueWorkspace(EditorWorkspaceHost& workspaceHost, const AssetGuid& assetGuid = {}) {
    EditorWorkspaceOpenRequest request{};
    request.workspaceId = EditorWorkspaceId::Dialogue;
    if (assetGuid.IsValid()) {
        request.dialogueAssetGuid = assetGuid;
    }
    (void)workspaceHost.RequestOpen(std::move(request));
}

void WriteDocumentState(const DialogueWorkspaceController& controller, EditorOperationResult& result) {
    result.data["workspace"] = "dialogue";
    result.data["open"] = controller.IsDocumentOpen();
    result.data["dirty"] = controller.IsDocumentDirty();
    const AssetGuid& guid = controller.GetDocumentAssetGuid();
    result.data["assetGuid"] = guid.IsValid() ? guid.value : std::string{};
    if (controller.IsDocumentOpen()) {
        result.data["name"] = controller.GetDefinition().name;
    }
}
} // namespace

void RegisterDialogueOperations(EditorOperationRegistry& registry, DialogueWorkspaceController& controller,
                                EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> sceneProvider) {
    auto assetQuery = EditorOperationDefinition{
        "dialogue.list_assets",
        "List Dialogue assets in the current project.",
        "dialogue",
        {},
        [&controller, sceneProvider](const nlohmann::json&) {
            EditorOperationResult result{};
            DocumentSceneBase* scene = nullptr;
            if (!RequireDialogueScene(sceneProvider, result, scene))
                return result;
            result.data["assets"] = nlohmann::json::array();
            for (const DialogueAssetListEntry& asset : controller.ListAssets(scene->GetAssetDatabase())) {
                nlohmann::json item{
                    {"guid", asset.guid.value},
                    {"name", asset.name},
                    {"path", asset.sourcePath.generic_string()},
                    {"open", asset.current},
                };
                result.data["assets"].push_back(item);
                result.details.push_back(item.dump());
            }
            result.ok = true;
            result.code = "OK";
            result.message = "Returned " + std::to_string(result.data["assets"].size()) + " Dialogue assets.";
            return result;
        },
    };
    assetQuery.requiresDocumentSession = false;
    assetQuery.kind = EditorOperationKind::Query;
    registry.Register(std::move(assetQuery));
    {
        EditorOperationDefinition transition{
            "dialogue.new_asset",
            "Create a new formal Dialogue document.",
            "dialogue",
            {},
            [&controller, &workspaceHost, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireDialogueScene(sceneProvider, result, scene))
                    return result;
                std::string message{};
                if (!controller.RequestNewAsset(message)) {
                    if (controller.HasPendingConfirmation()) {
                        return PendingConfirmation("Current Dialogue has unsaved changes; creation "
                                                   "has not run.");
                    }
                    return OperationFailure("NEW_ASSET_FAILED", std::move(message));
                }
                RevealDialogueWorkspace(workspaceHost);
                result.ok = true;
                result.code = "OK";
                result.message = std::move(message);
                WriteDocumentState(controller, result);
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }
    {
        EditorOperationDefinition transition{
            "dialogue.open_asset",
            "Open a Dialogue asset by guid.",
            "dialogue",
            {{"assetGuid", EditorOperationParameterType::String, true, "Dialogue asset guid"}},
            [&controller, &workspaceHost, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireDialogueScene(sceneProvider, result, scene))
                    return result;
                const AssetGuid guid{parameters.value("assetGuid", std::string{})};
                if (!guid.IsValid()) {
                    return OperationFailure("MISSING_FIELD", "assetGuid is required.");
                }
                const AssetRecord* record = scene->GetAssetDatabase().FindByGuid(guid);
                if (record == nullptr || record->type != AssetType::Dialogue) {
                    return OperationFailure("ASSET_NOT_FOUND", "Dialogue asset was not found.");
                }
                std::string message{};
                if (!controller.RequestOpenAsset(scene->GetAssetDatabase(), guid, message)) {
                    if (controller.HasPendingConfirmation()) {
                        return PendingConfirmation("Current Dialogue has unsaved changes; the new "
                                                   "asset has not been opened.");
                    }
                    return OperationFailure("OPEN_FAILED", std::move(message));
                }
                RevealDialogueWorkspace(workspaceHost, guid);
                result.ok = true;
                result.code = "OK";
                result.message = std::move(message);
                WriteDocumentState(controller, result);
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }
    registry.Register(
        {
            "dialogue.save",
            "Validate and save the current Dialogue document.",
            "dialogue",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result))
                    return result;
                DocumentSceneBase* scene = nullptr;
                if (!RequireDialogueScene(sceneProvider, result, scene))
                    return result;
                std::string message{};
                result.ok = controller.SaveDialogue(*scene, message);
                result.code = result.ok ? "OK" : "SAVE_FAILED";
                result.message = std::move(message);
                WriteDocumentState(controller, result);
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    {
        EditorOperationDefinition transition{
            "dialogue.revert",
            "Revert the current saved Dialogue asset.",
            "dialogue",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result))
                    return result;
                DocumentSceneBase* scene = nullptr;
                if (!RequireDialogueScene(sceneProvider, result, scene))
                    return result;
                std::string message{};
                if (!controller.RequestRevert(scene->GetAssetDatabase(), message)) {
                    if (controller.HasPendingConfirmation()) {
                        return PendingConfirmation("Revert requires confirmation because the current "
                                                   "Dialogue is dirty.");
                    }
                    return OperationFailure("REVERT_FAILED", std::move(message));
                }
                result.ok = true;
                result.code = "OK";
                result.message = std::move(message);
                WriteDocumentState(controller, result);
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }
    {
        EditorOperationDefinition transition{
            "dialogue.close_asset",
            "Close the current Dialogue asset.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                std::string message{};
                if (!controller.RequestCloseAsset(message)) {
                    if (controller.HasPendingConfirmation()) {
                        return PendingConfirmation("Closing the current Dialogue requires "
                                                   "confirmation because it is dirty.");
                    }
                    return OperationFailure("CLOSE_FAILED", std::move(message));
                }
                result.ok = true;
                result.code = "OK";
                result.message = std::move(message);
                WriteDocumentState(controller, result);
                return result;
            },
        };
        transition.requiresCleanDocument = true;
        registry.Register(std::move(transition), EditorOperationKind::Session);
    }
    registry.Register(
        {
            "dialogue.undo",
            "Undo the latest Dialogue edit.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result))
                    return result;
                std::string message{};
                result.ok = controller.Undo(message);
                result.code = result.ok ? "OK" : "UNDO_FAILED";
                result.message = std::move(message);
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {
            "dialogue.redo",
            "Redo the latest undone Dialogue edit.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result))
                    return result;
                std::string message{};
                result.ok = controller.Redo(message);
                result.code = result.ok ? "OK" : "REDO_FAILED";
                result.message = std::move(message);
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {
            "dialogue.query",
            "Return the current Dialogue node graph and validation issues.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result))
                    return result;
                const auto& definition = controller.GetDefinition();
                result.ok = true;
                result.code = "OK";
                result.message = definition.name + " (entry=" + definition.entryNodeId + ")";
                for (const DIALOGUE::DialogueNode& node : definition.nodes) {
                    result.details.push_back(node.id + " [" + DIALOGUE::ToString(node.type) + "]");
                }
                for (const auto& issue : DIALOGUE::ValidateDialogue(definition)) {
                    result.details.push_back(std::string(issue.error ? "ERROR " : "WARN ") + issue.path + ": " +
                                             issue.message);
                }
                WriteDocumentState(controller, result);
                result.data["entryNodeId"] = definition.entryNodeId;
                result.data["nodeCount"] = definition.nodes.size();
                return result;
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Query);
}

} // namespace HIKARI::EDITOR
