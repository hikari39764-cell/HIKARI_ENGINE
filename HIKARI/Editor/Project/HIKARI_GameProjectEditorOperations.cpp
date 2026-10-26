#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {
void GameProjectEditorWorkspaceRegistry::SetOperationRegistry(EditorOperationRegistry* registry) noexcept {
    operationRegistry_ = registry;
}

void GameProjectEditorWorkspaceRegistry::BeginOperationScope(EditorOperationRegistry& registry) noexcept {
    if (operationScopeDepth_++ == 0u) {
        scopeRegistry_ = &registry;
    }
}

void GameProjectEditorWorkspaceRegistry::EndOperationScope() noexcept {
    if (operationScopeDepth_ > 0u && --operationScopeDepth_ == 0u) {
        scopeRegistry_ = nullptr;
    }
}

void GameProjectEditorWorkspaceRegistry::ExecuteOperation(std::string_view id, std::string_view parametersJson,
                                                          EDITOR_PROJECT::IEditorOperationResultSink& sink) {
    EditorOperationResult result;
    if (std::this_thread::get_id() != ownerThread_) {
        sink.Receive(false, "WRONG_THREAD", "Workspace operations run on the Editor owner thread.", "{}");
        return;
    }
    // 工作区の描画の中か、項目の Editor 操作の処理の中だけ。
    EditorOperationRegistry* const registry = frameUi_ ? operationRegistry_ : operationScopeDepth_ > 0u ? scopeRegistry_ : nullptr;
    if (!registry || executingOperation_) {
        sink.Receive(false, "UNAVAILABLE",
                     "Operation calls require an active workspace callback or a project operation handler; recursive "
                     "execution is rejected.",
                     "{}");
        return;
    }
    // Keep the guard alive through the result callback as well.
    executingOperation_ = true;
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{executingOperation_};
    try {
        const auto* definition = registry->Find(std::string(id));
        // 未登録と公開対象外を分け、項目 UI が古い Engine と権限不足を区別できるようにする。
        if (!definition) {
            result = {false, "OPERATION_NOT_FOUND", "Unregistered editor operation: " + std::string(id)};
        } else if (!HasEditorOperationExposure(definition->exposureMask, EditorOperationExposureMask::Human)) {
            result = {false, "UNAVAILABLE", "The operation is not exposed to workspace controls."};
        } else if (definition->executionPolicy != EditorOperationExecutionPolicy::MainThread ||
                   (definition->permission == EditorOperationPermission::ExecuteExternalProcess &&
                    definition->kind != EditorOperationKind::Session)) {
            result = {false, "ASYNC_REQUIRED", "This action must use its owning background job workflow."};
        } else if (parametersJson.size() > 1024u * 1024u) {
            result = {false, "TOO_LARGE", "Operation parameters exceed 1 MiB."};
        } else {
            const auto parameters = nlohmann::json::parse(parametersJson, nullptr, false);
            result = parameters.is_object()
                         ? registry->Execute(std::string(id), parameters)
                         : EditorOperationResult{false, "INVALID_JSON", "Operation parameters must be a JSON object."};
        }
    } catch (const std::exception& error) {
        result = {false, "OPERATION_FAILED", error.what()};
    } catch (...) {
        result = {false, "OPERATION_FAILED", "Unknown operation exception."};
    }
    sink.Receive(result.ok, result.code, result.message, result.data.dump());
}
} // namespace HIKARI::EDITOR
