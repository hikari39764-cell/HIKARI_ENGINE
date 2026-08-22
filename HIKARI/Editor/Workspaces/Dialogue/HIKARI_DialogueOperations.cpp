#include "Editor/Workspaces/Dialogue/HIKARI_DialogueOperations.h"

#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
    namespace {
        EditorOperationParameter Parameter(
            std::string name,
            EditorOperationParameterType type,
            bool required,
            std::string description) {
            EditorOperationParameter value{};
            value.name = std::move(name);
            value.type = type;
            value.required = required;
            value.description = std::move(description);
            return value;
        }

        bool RequireScene(
            const std::function<DocumentSceneBase*()>& provider,
            EditorOperationResult& result,
            DocumentSceneBase*& outScene) {
            outScene = provider ? provider() : nullptr;
            if (outScene != nullptr) return true;
            result.code = "NO_ACTIVE_SCENE";
            result.message = "No active scene owns the dialogue workspace.";
            return false;
        }

        bool RequireDocument(
            DialogueWorkspaceController& controller,
            EditorOperationResult& result) {
            if (controller.IsDocumentOpen()) return true;
            result.code = "NO_DIALOGUE_DOCUMENT";
            result.message = "Open or create a dialogue document first.";
            return false;
        }
    }

    void RegisterDialogueOperations(
        EditorOperationRegistry& registry,
        DialogueWorkspaceController& controller,
        std::function<DocumentSceneBase*()> sceneProvider) {
        registry.Register({
            "dialogue.new_asset",
            "Create a new formal Dialogue document.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                std::string message{};
                result.ok = controller.NewAsset(message);
                result.code = result.ok ? "OK" : "UNSAVED_CHANGES";
                result.message = std::move(message);
                return result;
            },
        });
        registry.Register({
            "dialogue.open_asset",
            "Open a Dialogue asset by guid.",
            "dialogue",
            { Parameter(
                "assetGuid", EditorOperationParameterType::String,
                true, "Dialogue asset guid") },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result{};
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) return result;
                const AssetGuid guid{
                    parameters.value("assetGuid", std::string{}) };
                if (!guid.IsValid()) {
                    result.code = "MISSING_FIELD";
                    result.message = "assetGuid is required.";
                    return result;
                }
                const AssetRecord* record =
                    scene->GetAssetDatabase().FindByGuid(guid);
                if (record == nullptr || record->type != AssetType::Dialogue) {
                    result.code = "ASSET_NOT_FOUND";
                    result.message = "Dialogue asset was not found.";
                    return result;
                }
                std::string message{};
                result.ok = controller.OpenAsset(
                    scene->GetAssetDatabase(), guid, message);
                result.code = result.ok ? "OK" : "OPEN_FAILED";
                result.message = std::move(message);
                return result;
            },
        });
        registry.Register({
            "dialogue.save",
            "Validate and save the current Dialogue document.",
            "dialogue",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result)) return result;
                DocumentSceneBase* scene = nullptr;
                if (!RequireScene(sceneProvider, result, scene)) return result;
                std::string message{};
                result.ok = controller.Save(*scene, message);
                result.code = result.ok ? "OK" : "SAVE_FAILED";
                result.message = std::move(message);
                return result;
            },
        });
        registry.Register({
            "dialogue.undo",
            "Undo the latest Dialogue edit.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result)) return result;
                std::string message{};
                result.ok = controller.Undo(message);
                result.code = result.ok ? "OK" : "UNDO_FAILED";
                result.message = std::move(message);
                return result;
            },
        });
        registry.Register({
            "dialogue.redo",
            "Redo the latest undone Dialogue edit.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result)) return result;
                std::string message{};
                result.ok = controller.Redo(message);
                result.code = result.ok ? "OK" : "REDO_FAILED";
                result.message = std::move(message);
                return result;
            },
        });
        registry.Register({
            "dialogue.query",
            "Return the current Dialogue node graph and validation issues.",
            "dialogue",
            {},
            [&controller](const nlohmann::json&) {
                EditorOperationResult result{};
                if (!RequireDocument(controller, result)) return result;
                const auto& definition = controller.GetDefinition();
                result.ok = true;
                result.code = "OK";
                result.message = definition.name + " (entry=" +
                    definition.entryNodeId + ")";
                for (const DIALOGUE::DialogueNode& node : definition.nodes) {
                    result.details.push_back(
                        node.id + " [" + DIALOGUE::ToString(node.type) + "]");
                }
                for (const auto& issue : DIALOGUE::ValidateDialogue(definition)) {
                    result.details.push_back(
                        std::string(issue.error ? "ERROR " : "WARN ") +
                        issue.path + ": " + issue.message);
                }
                return result;
            },
        });
    }

} // namespace HIKARI::EDITOR
