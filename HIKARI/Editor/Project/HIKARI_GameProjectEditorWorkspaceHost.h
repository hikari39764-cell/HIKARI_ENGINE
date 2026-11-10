#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Editor/Project/HIKARI_ModuleEditorExtensions.h"
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
    // 作業区ごとの視口窓をその作業区のドックへ入れる準備をし、窓の固定 ID を返す。視口窓を作業区の間で共有すると、
    // 別の作業区のドックに入ったまま隠れて残り、他の作業区では表示されなくなる。
    std::string PrepareSceneViewportWindow();
    void Shutdown() noexcept;

    bool IsAvailable() const noexcept;
    ModuleEditorExtensions& Extensions() noexcept { return extensions_; }
    // 場景の作業区でモジュールの窓を描く。
    void DrawModulePanels(AssetDatabase&, AssetRegistry&);
    GameProjectEditorWorkspaceRegistry& Registry() noexcept;
    const GameProjectEditorWorkspaceRegistry& Registry() const noexcept;

private:
    void DrawWorkspaceSelector(EditorWorkspaceHost& host);
    void DrawClosePrompt();

    GameProjectEditorWorkspaceRegistry registry_{};
    std::unique_ptr<GameProjectEditorDocumentService> documents_{};
    EditorOperationRegistry* operationRegistry_ = nullptr;
    std::vector<uint64_t> operationOwnerTokens_{};
    ModuleEditorExtensions extensions_{};
    std::unique_ptr<GameProjectEditorWorkspaceUi> ui_{};
    std::string moduleFailureMessage_{};
    bool closePromptWasPending_ = false;
    std::string requestedWorkspace_{};
    std::optional<EditorWorkspaceOpenRequest> guardedNavigation_{};
    bool resetLayout_ = false;
};

} // namespace HIKARI::EDITOR
