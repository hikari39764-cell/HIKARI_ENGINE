#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
} // namespace HIKARI
namespace HIKARI::EDITOR {

class GameProjectEditorModuleLibrary;
class GameProjectEditorDocumentService;
class GameProjectEditorWorkspaceUi;
class EditorOperationRegistry;

class GameProjectEditorWorkspaceHost {
public:
    GameProjectEditorWorkspaceHost();
    ~GameProjectEditorWorkspaceHost();

    GameProjectEditorWorkspaceHost(const GameProjectEditorWorkspaceHost&) = delete;
    GameProjectEditorWorkspaceHost& operator=(const GameProjectEditorWorkspaceHost&) = delete;

    bool RegisterModule(const GameProjectEditorModuleLibrary& module, std::string_view projectRootUtf8,
                        EditorOperationRegistry& operationRegistry, std::string& outMessage);
    void ReportModuleFailure(std::string message);
    void RequestWorkspace(std::string workspaceId) { requestedWorkspace_ = std::move(workspaceId); }
    void UpdateNavigation(EditorWorkspaceHost& host, float unscaledDeltaSeconds);
    void DrawWorkspaceEntries(EditorWorkspaceHost& host, const char* filter);
    void Draw(EditorWorkspaceHost& host, float unscaledDeltaSeconds, AssetDatabase&, AssetRegistry&);
    bool ActiveWorkspaceRequestsSceneViewport() const noexcept;
    bool HasUnsavedChanges() const noexcept;
    void PrepareSceneViewportWindow();
    void Shutdown() noexcept;

    bool IsAvailable() const noexcept;
    GameProjectEditorWorkspaceRegistry& Registry() noexcept;
    const GameProjectEditorWorkspaceRegistry& Registry() const noexcept;

private:
    void DrawWorkspaceSelector(EditorWorkspaceHost& host);
    void DrawClosePrompt();

    GameProjectEditorWorkspaceRegistry registry_{};
    std::unique_ptr<GameProjectEditorDocumentService> documents_{};
    EditorOperationRegistry* operationRegistry_ = nullptr;
    uint64_t operationOwnerToken_ = 0u;
    std::unique_ptr<GameProjectEditorWorkspaceUi> ui_{};
    std::string moduleFailureMessage_{};
    bool closePromptWasPending_ = false;
    std::string requestedWorkspace_{};
    std::optional<EditorWorkspaceOpenRequest> guardedNavigation_{};
    bool resetLayout_ = false;
};

} // namespace HIKARI::EDITOR
