#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {
    void GameProjectEditorWorkspaceRegistry::SetOperationRegistry(EditorOperationRegistry* registry) noexcept {
        operationRegistry_ = registry;
    }

    void GameProjectEditorWorkspaceRegistry::ExecuteOperation(std::string_view id,
        std::string_view parametersJson, EDITOR_PROJECT::IEditorOperationResultSink& sink) {
        EditorOperationResult result;
        if (std::this_thread::get_id() != ownerThread_) {
            sink.Receive(false, "WRONG_THREAD", "Workspace operations run on the Editor owner thread.", "{}");
            return;
        }
        if (!frameUi_ || !operationRegistry_ || executingOperation_) {
            sink.Receive(false, "UNAVAILABLE", "Operation calls require an active workspace callback; recursive execution is rejected.", "{}");
            return;
        }
        // Keep the guard alive through the result callback as well.
        executingOperation_ = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{executingOperation_};
        try {
            const auto* definition = operationRegistry_->Find(std::string(id));
            if (!definition || !HasEditorOperationExposure(definition->exposureMask, EditorOperationExposureMask::Human)) {
                result = {false, "UNAVAILABLE", "The operation is not exposed to workspace controls."};
            } else if (definition->executionPolicy != EditorOperationExecutionPolicy::MainThread ||
                (definition->permission == EditorOperationPermission::ExecuteExternalProcess && definition->kind != EditorOperationKind::Session)) {
                result = {false, "ASYNC_REQUIRED", "This action must use its owning background job workflow."};
            } else if (parametersJson.size() > 1024u * 1024u) {
                result = {false, "TOO_LARGE", "Operation parameters exceed 1 MiB."};
            } else {
                const auto parameters = nlohmann::json::parse(parametersJson, nullptr, false);
                result = parameters.is_object() ? operationRegistry_->Execute(std::string(id), parameters)
                    : EditorOperationResult{false, "INVALID_JSON", "Operation parameters must be a JSON object."};
            }
        } catch (const std::exception& error) {
            result = {false, "OPERATION_FAILED", error.what()};
        } catch (...) {
            result = {false, "OPERATION_FAILED", "Unknown operation exception."};
        }
        sink.Receive(result.ok, result.code, result.message, result.data.dump());
    }
}
