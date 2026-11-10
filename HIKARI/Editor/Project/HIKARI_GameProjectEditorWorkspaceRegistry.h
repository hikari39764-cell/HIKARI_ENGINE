#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "HIKARI/Project/EditorWorkspace.h"

namespace HIKARI::EDITOR {

// GameProject の editor 拡張が登録する module・workspace・操作の ID。ASCII 英数字で始まり、
// 英数字と . _ - だけの 127 文字以内。
bool IsGameProjectEditorIdentifier(std::string_view value) noexcept;

class EditorOperationRegistry;

enum class GameProjectWorkspaceCloseResolution : uint8_t {
    Save,
    Discard,
    Cancel,
};

struct GameProjectEditorWorkspaceInfo {
    std::string_view moduleId{};
    std::string_view workspaceId{};
    std::string_view displayName{};
    std::string_view failureMessage{};
    bool active = false;
    bool disabled = false;
};

// Owns host-side workspace identity and transition state. Workspace
// instances remain owned by the project DLL and must outlive this registry.
class GameProjectEditorWorkspaceRegistry final : public EDITOR_PROJECT::IEditorWorkspaceCommands {
public:
    static constexpr size_t kMaximumWorkspaceCount = 64u;

    bool RegisterModule(std::string_view moduleId, EDITOR_PROJECT::IEditorWorkspace* const* workspaces,
                        size_t workspaceCount, std::string& outMessage);
    void SetProjectRootUtf8(std::string projectRootUtf8);
    void SetDocuments(EDITOR_PROJECT::IEditorDocumentService* documents) noexcept;
    // The controller owns the operation registry; attach only for the frame.
    void SetOperationRegistry(EditorOperationRegistry* registry) noexcept;
    // 項目の Editor 操作の処理の間（入れ子可）。この間は工作区の描画の外でも ExecuteOperation が使える。
    void BeginOperationScope(EditorOperationRegistry& registry) noexcept;
    void EndOperationScope() noexcept;

    size_t GetWorkspaceCount() const noexcept;
    GameProjectEditorWorkspaceInfo GetWorkspaceInfo(size_t index) const noexcept;
    bool HasActiveWorkspace() const noexcept;
    std::string_view GetActiveWorkspaceId() const noexcept;
    bool ActiveWorkspaceRequestsSceneViewport() const noexcept;
    bool HasUnsavedWorkspaceChanges() const noexcept;
    bool IsActiveWorkspaceDirty() noexcept;
    // モジュールが登記した資産の種類を編集する工作区の ID（無ければ空）。
    std::string_view FindWorkspaceForAssetType(std::string_view moduleAssetTypeId) const noexcept;
    // その工作区が次に描かれる前に、資産を開かせる（IEditorWorkspace::OpenAsset）。工作区の切り替えは呼び手が頼む。
    void SetPendingAsset(std::string_view workspaceId, std::string_view assetGuid);

    void BeginFrame(EDITOR_PROJECT::IEditorWorkspaceUi& ui, float unscaledDeltaSeconds) noexcept;
    void ProcessPending() noexcept;
    void DrawActive() noexcept;
    void EndFrame() noexcept;

    bool IsAwaitingCloseResolution() const noexcept;
    std::string_view GetAwaitingWorkspaceName() const noexcept;
    void ResolvePendingClose(GameProjectWorkspaceCloseResolution resolution) noexcept;

    std::string_view GetStatusMessage() const noexcept;
    void ClearStatus() noexcept;

    // Host teardown always releases project callbacks before the DLL is
    // unloaded. QueryClose is advisory during process shutdown.
    void Shutdown(EDITOR_PROJECT::IEditorWorkspaceUi& ui) noexcept;

    bool RequestActivateWorkspace(std::string_view workspaceId) override;
    bool RequestCloseActiveWorkspace() override;
    bool RequestSaveActiveWorkspace() override;
    void PublishStatus(EDITOR_PROJECT::EditorWorkspaceStatusSeverity severity, std::string_view message) override;
    void ExecuteOperation(std::string_view operationId, std::string_view parametersJson,
                          EDITOR_PROJECT::IEditorOperationResultSink& result) override;

private:
    struct Entry {
        std::string moduleId{};
        std::string workspaceId{};
        std::string displayName{};
        EDITOR_PROJECT::IEditorWorkspace* workspace = nullptr;
        std::string failureMessage{};
        bool requestsSceneViewport = false;
        bool disabled = false;
        // この工作区が編集する、モジュールの資産の種類（無ければ空）。
        std::string assetTypeId{};
    };

    struct NavigationRequest {
        bool pending = false;
        std::optional<size_t> target{};
        EDITOR_PROJECT::EditorWorkspaceCloseReason reason = EDITOR_PROJECT::EditorWorkspaceCloseReason::SwitchWorkspace;
    };

    EDITOR_PROJECT::EditorWorkspaceHostContext MakeContext(const Entry& entry) noexcept;
    bool Activate(size_t index) noexcept;
    void Deactivate(size_t index) noexcept;
    void ApplyNavigation(std::optional<size_t> target) noexcept;
    void Disable(size_t index, std::string_view phase, std::string_view detail) noexcept;
    void ProcessSaveRequest() noexcept;
    void ProcessNavigationRequest() noexcept;
    std::optional<size_t> Find(std::string_view workspaceId) const noexcept;

    std::vector<Entry> entries_{};
    std::string projectRootUtf8_{};
    EDITOR_PROJECT::IEditorDocumentService* documents_ = nullptr;
    std::optional<size_t> active_{};
    // 資産浏览器から開くよう頼まれた資産と、その行き先の工作区。
    std::optional<size_t> pendingAssetWorkspace_{};
    std::string pendingAssetGuid_{};
    NavigationRequest navigation_{};
    std::optional<NavigationRequest> awaitingClose_{};
    bool saveRequested_ = false;
    EditorOperationRegistry* operationRegistry_ = nullptr;
    EditorOperationRegistry* scopeRegistry_ = nullptr;
    uint32_t operationScopeDepth_ = 0u;
    const std::thread::id ownerThread_ = std::this_thread::get_id();
    bool executingOperation_ = false;
    EDITOR_PROJECT::IEditorWorkspaceUi* frameUi_ = nullptr;
    uint64_t frameIndex_ = 0u;
    float frameDeltaSeconds_ = 0.0f;
    std::string statusMessage_{};
    EDITOR_PROJECT::EditorWorkspaceStatusSeverity statusSeverity_ = EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Info;
};

} // namespace HIKARI::EDITOR
