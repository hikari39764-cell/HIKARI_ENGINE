#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace HIKARI::EDITOR {
namespace {

    bool IsIdentifier(std::string_view value) noexcept {
        if (value.empty() || value.size() > 127u) {
            return false;
        }
        const auto isAsciiAlphaNumeric = [](unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
        };
        if (!isAsciiAlphaNumeric(
                static_cast<unsigned char>(value.front()))) {
            return false;
        }
        return std::all_of(
            value.begin(),
            value.end(),
            [isAsciiAlphaNumeric](unsigned char character) {
                return isAsciiAlphaNumeric(character) ||
                    character == '.' || character == '_' ||
                    character == '-';
            });
    }

    bool HasReservedEnginePrefix(std::string_view value) noexcept {
        constexpr std::string_view prefix = "hikari.";
        if (value.size() < prefix.size()) {
            return false;
        }
        for (size_t index = 0u; index < prefix.size(); ++index) {
            if (static_cast<unsigned char>(std::tolower(
                    static_cast<unsigned char>(value[index]))) !=
                static_cast<unsigned char>(prefix[index])) {
                return false;
            }
        }
        return true;
    }

    std::string ExceptionDetail(const std::exception& exception) {
        const char* detail = exception.what();
        return detail != nullptr && detail[0] != '\0'
            ? detail
            : "unknown standard exception";
    }

} // namespace

    bool GameProjectEditorWorkspaceRegistry::RegisterModule(
        std::string_view moduleId,
        EDITOR_PROJECT::IEditorWorkspace* const* workspaces,
        size_t workspaceCount,
        std::string& outMessage) {

        if (!IsIdentifier(moduleId)) {
            outMessage =
                "GameProject Editor module ID must be a non-empty ASCII "
                "identifier no longer than 127 characters.";
            return false;
        }
        if (workspaceCount > kMaximumWorkspaceCount ||
            entries_.size() > kMaximumWorkspaceCount - workspaceCount) {
            outMessage =
                "GameProject Editor workspace registration exceeds the "
                "64-workspace host limit.";
            return false;
        }
        if (workspaceCount != 0u && workspaces == nullptr) {
            outMessage =
                "GameProject Editor workspace registration is missing its "
                "workspace pointer table.";
            return false;
        }

        std::vector<Entry> staged{};
        staged.reserve(workspaceCount);
        std::unordered_set<std::string> stagedIds{};
        for (size_t index = 0u; index < workspaceCount; ++index) {
            EDITOR_PROJECT::IEditorWorkspace* workspace = workspaces[index];
            if (workspace == nullptr) {
                outMessage =
                    "GameProject Editor module returned a null workspace.";
                return false;
            }

            EDITOR_PROJECT::EditorWorkspaceDescriptor descriptor{};
            try {
                descriptor = workspace->GetDescriptor();
            }
            catch (const std::exception& exception) {
                outMessage =
                    "GameProject Editor workspace descriptor threw: " +
                    ExceptionDetail(exception);
                return false;
            }
            catch (...) {
                outMessage =
                    "GameProject Editor workspace descriptor threw an "
                    "unknown exception.";
                return false;
            }

            const std::string_view workspaceId =
                descriptor.workspaceId != nullptr
                ? std::string_view(descriptor.workspaceId)
                : std::string_view{};
            const std::string_view displayName =
                descriptor.displayName != nullptr
                ? std::string_view(descriptor.displayName)
                : std::string_view{};
            if (descriptor.structSize <
                    sizeof(EDITOR_PROJECT::EditorWorkspaceDescriptor) ||
                descriptor.apiVersion !=
                    EDITOR_PROJECT::kEditorWorkspaceApiVersion) {
                outMessage =
                    "GameProject Editor workspace descriptor version is "
                    "incompatible.";
                return false;
            }
            if (!IsIdentifier(workspaceId) ||
                HasReservedEnginePrefix(workspaceId)) {
                outMessage =
                    "GameProject Editor workspace ID is invalid or uses "
                    "the reserved 'hikari.' namespace: " +
                    std::string(workspaceId);
                return false;
            }
            if (displayName.empty() || displayName.size() > 255u) {
                outMessage =
                    "GameProject Editor workspace display name must contain "
                    "1 to 255 UTF-8 bytes.";
                return false;
            }
            if (Find(workspaceId) ||
                !stagedIds.emplace(workspaceId).second) {
                outMessage =
                    "GameProject Editor workspace ID conflicts with an "
                    "existing registration: " + std::string(workspaceId);
                return false;
            }
            if (std::any_of(
                    entries_.begin(),
                    entries_.end(),
                    [workspace](const Entry& entry) {
                        return entry.workspace == workspace;
                    }) ||
                std::any_of(
                    staged.begin(),
                    staged.end(),
                    [workspace](const Entry& entry) {
                        return entry.workspace == workspace;
                    })) {
                outMessage =
                    "GameProject Editor workspace instance was registered "
                    "more than once.";
                return false;
            }

            staged.push_back(Entry{
                std::string(moduleId),
                std::string(workspaceId),
                std::string(displayName),
                workspace,
                {},
                false,
            });
        }

        entries_.insert(
            entries_.end(),
            std::make_move_iterator(staged.begin()),
            std::make_move_iterator(staged.end()));
        outMessage.clear();
        return true;
    }

    void GameProjectEditorWorkspaceRegistry::SetProjectRootUtf8(
        std::string projectRootUtf8) {
        if (!entries_.empty()) {
            return;
        }
        projectRootUtf8_ = std::move(projectRootUtf8);
    }

    size_t GameProjectEditorWorkspaceRegistry::
        GetWorkspaceCount() const noexcept {
        return entries_.size();
    }

    GameProjectEditorWorkspaceInfo
        GameProjectEditorWorkspaceRegistry::GetWorkspaceInfo(
            size_t index) const noexcept {
        if (index >= entries_.size()) {
            return {};
        }
        const Entry& entry = entries_[index];
        return {
            entry.moduleId,
            entry.workspaceId,
            entry.displayName,
            entry.failureMessage,
            active_ && *active_ == index,
            entry.disabled,
        };
    }

    bool GameProjectEditorWorkspaceRegistry::
        HasActiveWorkspace() const noexcept {
        return active_.has_value();
    }

    std::string_view GameProjectEditorWorkspaceRegistry::
        GetActiveWorkspaceId() const noexcept {
        return active_ && *active_ < entries_.size()
            ? std::string_view(entries_[*active_].workspaceId)
            : std::string_view{};
    }

    bool GameProjectEditorWorkspaceRegistry::
        IsActiveWorkspaceDirty() noexcept {
        if (!active_ || *active_ >= entries_.size()) {
            return false;
        }
        Entry& entry = entries_[*active_];
        if (entry.disabled || entry.workspace == nullptr) {
            return false;
        }
        try {
            return entry.workspace->IsDirty();
        }
        catch (const std::exception& exception) {
            Disable(*active_, "IsDirty", ExceptionDetail(exception));
        }
        catch (...) {
            Disable(*active_, "IsDirty", "unknown exception");
        }
        return false;
    }

    void GameProjectEditorWorkspaceRegistry::BeginFrame(
        EDITOR_PROJECT::IEditorWorkspaceUi& ui,
        float unscaledDeltaSeconds) noexcept {

        frameUi_ = &ui;
        frameDeltaSeconds_ = std::max(0.0f, unscaledDeltaSeconds);
        ++frameIndex_;
    }

    void GameProjectEditorWorkspaceRegistry::ProcessPending() noexcept {
        if (frameUi_ == nullptr || awaitingClose_) {
            return;
        }
        ProcessSaveRequest();
        if (!awaitingClose_) {
            ProcessNavigationRequest();
        }
    }

    void GameProjectEditorWorkspaceRegistry::DrawActive() noexcept {
        if (frameUi_ == nullptr || !active_ ||
            *active_ >= entries_.size()) {
            return;
        }
        const size_t index = *active_;
        Entry& entry = entries_[index];
        if (entry.disabled || entry.workspace == nullptr) {
            return;
        }
        EDITOR_PROJECT::EditorWorkspaceHostContext context =
            MakeContext(entry);
        try {
            entry.workspace->Draw(context);
        }
        catch (const std::exception& exception) {
            Disable(index, "Draw", ExceptionDetail(exception));
            Deactivate(index);
            active_.reset();
        }
        catch (...) {
            Disable(index, "Draw", "unknown exception");
            Deactivate(index);
            active_.reset();
        }
    }

    void GameProjectEditorWorkspaceRegistry::EndFrame() noexcept {
        frameUi_ = nullptr;
        frameDeltaSeconds_ = 0.0f;
    }

    bool GameProjectEditorWorkspaceRegistry::
        IsAwaitingCloseResolution() const noexcept {
        return awaitingClose_.has_value();
    }

    std::string_view GameProjectEditorWorkspaceRegistry::
        GetAwaitingWorkspaceName() const noexcept {
        return awaitingClose_ && active_ && *active_ < entries_.size()
            ? std::string_view(entries_[*active_].displayName)
            : std::string_view{};
    }

    void GameProjectEditorWorkspaceRegistry::ResolvePendingClose(
        GameProjectWorkspaceCloseResolution resolution) noexcept {

        if (!awaitingClose_ || frameUi_ == nullptr || !active_ ||
            *active_ >= entries_.size()) {
            return;
        }
        if (resolution ==
            GameProjectWorkspaceCloseResolution::Cancel) {
            awaitingClose_.reset();
            PublishStatus(
                EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Info,
                "Workspace transition canceled.");
            return;
        }

        if (resolution == GameProjectWorkspaceCloseResolution::Save) {
            Entry& entry = entries_[*active_];
            EDITOR_PROJECT::EditorWorkspaceHostContext context =
                MakeContext(entry);
            try {
                const auto result = entry.workspace->Save(context);
                if (result !=
                    EDITOR_PROJECT::EditorWorkspaceSaveResult::Saved) {
                    PublishStatus(
                        result == EDITOR_PROJECT::
                            EditorWorkspaceSaveResult::Canceled
                            ? EDITOR_PROJECT::
                                EditorWorkspaceStatusSeverity::Info
                            : EDITOR_PROJECT::
                                EditorWorkspaceStatusSeverity::Error,
                        result == EDITOR_PROJECT::
                            EditorWorkspaceSaveResult::Canceled
                            ? "Workspace save was canceled."
                            : "Workspace save failed.");
                    return;
                }
            }
            catch (const std::exception& exception) {
                Disable(*active_, "Save", ExceptionDetail(exception));
                return;
            }
            catch (...) {
                Disable(*active_, "Save", "unknown exception");
                return;
            }
        }

        const std::optional<size_t> target = awaitingClose_->target;
        awaitingClose_.reset();
        ApplyNavigation(target);
    }

    std::string_view GameProjectEditorWorkspaceRegistry::
        GetStatusMessage() const noexcept {
        return statusMessage_;
    }

    EDITOR_PROJECT::EditorWorkspaceStatusSeverity
        GameProjectEditorWorkspaceRegistry::
            GetStatusSeverity() const noexcept {
        return statusSeverity_;
    }

    void GameProjectEditorWorkspaceRegistry::ClearStatus() noexcept {
        statusMessage_.clear();
        statusSeverity_ =
            EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Info;
    }

    void GameProjectEditorWorkspaceRegistry::Shutdown(
        EDITOR_PROJECT::IEditorWorkspaceUi& ui) noexcept {

        frameUi_ = &ui;
        frameDeltaSeconds_ = 0.0f;
        ++frameIndex_;
        if (active_ && *active_ < entries_.size()) {
            Entry& entry = entries_[*active_];
            if (!entry.disabled && entry.workspace != nullptr) {
                try {
                    (void)entry.workspace->QueryClose(
                        EDITOR_PROJECT::
                            EditorWorkspaceCloseReason::HostShutdown);
                }
                catch (...) {
                    Disable(*active_, "QueryClose", "shutdown exception");
                }
            }
            Deactivate(*active_);
        }
        active_.reset();
        navigation_ = {};
        awaitingClose_.reset();
        saveRequested_ = false;
        entries_.clear();
        projectRootUtf8_.clear();
        EndFrame();
    }

    bool GameProjectEditorWorkspaceRegistry::
        RequestActivateWorkspace(std::string_view workspaceId) {
        const std::optional<size_t> target = Find(workspaceId);
        if (!target || entries_[*target].disabled || awaitingClose_) {
            return false;
        }
        if (active_ && *active_ == *target) {
            return true;
        }
        navigation_ = {
            true,
            target,
            EDITOR_PROJECT::EditorWorkspaceCloseReason::SwitchWorkspace,
        };
        return true;
    }

    bool GameProjectEditorWorkspaceRegistry::
        RequestCloseActiveWorkspace() {
        if (!active_ || awaitingClose_) {
            return false;
        }
        navigation_ = {
            true,
            std::nullopt,
            EDITOR_PROJECT::EditorWorkspaceCloseReason::CloseWorkspace,
        };
        return true;
    }

    bool GameProjectEditorWorkspaceRegistry::
        RequestSaveActiveWorkspace() {
        if (!active_ || awaitingClose_ ||
            entries_[*active_].disabled) {
            return false;
        }
        saveRequested_ = true;
        return true;
    }

    void GameProjectEditorWorkspaceRegistry::PublishStatus(
        EDITOR_PROJECT::EditorWorkspaceStatusSeverity severity,
        std::string_view message) {
        statusSeverity_ = severity;
        statusMessage_.assign(message);
    }

    EDITOR_PROJECT::EditorWorkspaceHostContext
        GameProjectEditorWorkspaceRegistry::MakeContext(
            const Entry& entry) noexcept {
        return {
            sizeof(EDITOR_PROJECT::EditorWorkspaceHostContext),
            EDITOR_PROJECT::kEditorWorkspaceApiVersion,
            frameIndex_,
            frameDeltaSeconds_,
            0u,
            projectRootUtf8_.c_str(),
            entry.moduleId.c_str(),
            entry.workspaceId.c_str(),
            frameUi_,
            this,
        };
    }

    bool GameProjectEditorWorkspaceRegistry::Activate(
        size_t index) noexcept {
        if (frameUi_ == nullptr || index >= entries_.size() ||
            entries_[index].disabled ||
            entries_[index].workspace == nullptr) {
            return false;
        }
        Entry& entry = entries_[index];
        EDITOR_PROJECT::EditorWorkspaceHostContext context =
            MakeContext(entry);
        try {
            entry.workspace->OnActivate(context);
            active_ = index;
            PublishStatus(
                EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Info,
                "Activated project workspace '" +
                    entry.displayName + "'.");
            return true;
        }
        catch (const std::exception& exception) {
            Disable(index, "OnActivate", ExceptionDetail(exception));
        }
        catch (...) {
            Disable(index, "OnActivate", "unknown exception");
        }
        return false;
    }

    void GameProjectEditorWorkspaceRegistry::Deactivate(
        size_t index) noexcept {
        if (frameUi_ == nullptr || index >= entries_.size() ||
            entries_[index].workspace == nullptr) {
            return;
        }
        Entry& entry = entries_[index];
        EDITOR_PROJECT::EditorWorkspaceHostContext context =
            MakeContext(entry);
        try {
            entry.workspace->OnDeactivate(context);
        }
        catch (const std::exception& exception) {
            Disable(index, "OnDeactivate", ExceptionDetail(exception));
        }
        catch (...) {
            Disable(index, "OnDeactivate", "unknown exception");
        }
    }

    void GameProjectEditorWorkspaceRegistry::ApplyNavigation(
        std::optional<size_t> target) noexcept {
        if (active_ && *active_ < entries_.size()) {
            Deactivate(*active_);
        }
        active_.reset();
        if (target) {
            (void)Activate(*target);
        }
    }

    void GameProjectEditorWorkspaceRegistry::Disable(
        size_t index,
        std::string_view phase,
        std::string_view detail) noexcept {
        if (index >= entries_.size()) {
            return;
        }
        Entry& entry = entries_[index];
        entry.disabled = true;
        entry.failureMessage =
            "Workspace '" + entry.workspaceId + "' failed during " +
            std::string(phase) + ": " + std::string(detail);
        PublishStatus(
            EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Error,
            entry.failureMessage);
    }

    void GameProjectEditorWorkspaceRegistry::
        ProcessSaveRequest() noexcept {
        if (!saveRequested_) {
            return;
        }
        saveRequested_ = false;
        if (!active_ || *active_ >= entries_.size()) {
            return;
        }
        Entry& entry = entries_[*active_];
        if (entry.disabled || entry.workspace == nullptr) {
            return;
        }
        EDITOR_PROJECT::EditorWorkspaceHostContext context =
            MakeContext(entry);
        try {
            const auto result = entry.workspace->Save(context);
            PublishStatus(
                result == EDITOR_PROJECT::EditorWorkspaceSaveResult::Saved
                    ? EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Info
                    : EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Error,
                result == EDITOR_PROJECT::EditorWorkspaceSaveResult::Saved
                    ? "Workspace saved."
                    : "Workspace save did not complete.");
        }
        catch (const std::exception& exception) {
            Disable(*active_, "Save", ExceptionDetail(exception));
        }
        catch (...) {
            Disable(*active_, "Save", "unknown exception");
        }
    }

    void GameProjectEditorWorkspaceRegistry::
        ProcessNavigationRequest() noexcept {
        if (!navigation_.pending) {
            return;
        }
        NavigationRequest request = navigation_;
        navigation_ = {};
        if (!active_ || *active_ >= entries_.size()) {
            ApplyNavigation(request.target);
            return;
        }

        Entry& entry = entries_[*active_];
        if (entry.disabled || entry.workspace == nullptr) {
            ApplyNavigation(request.target);
            return;
        }

        EDITOR_PROJECT::EditorWorkspaceCloseDisposition disposition =
            EDITOR_PROJECT::EditorWorkspaceCloseDisposition::Block;
        try {
            disposition = entry.workspace->QueryClose(request.reason);
        }
        catch (const std::exception& exception) {
            Disable(*active_, "QueryClose", ExceptionDetail(exception));
            ApplyNavigation(request.target);
            return;
        }
        catch (...) {
            Disable(*active_, "QueryClose", "unknown exception");
            ApplyNavigation(request.target);
            return;
        }

        switch (disposition) {
        case EDITOR_PROJECT::EditorWorkspaceCloseDisposition::Allow:
            ApplyNavigation(request.target);
            break;
        case EDITOR_PROJECT::EditorWorkspaceCloseDisposition::RequiresSave:
            awaitingClose_ = request;
            break;
        case EDITOR_PROJECT::EditorWorkspaceCloseDisposition::Block:
        default:
            PublishStatus(
                EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Warning,
                "Workspace blocked the requested transition.");
            break;
        }
    }

    std::optional<size_t> GameProjectEditorWorkspaceRegistry::Find(
        std::string_view workspaceId) const noexcept {
        for (size_t index = 0u; index < entries_.size(); ++index) {
            if (entries_[index].workspaceId == workspaceId) {
                return index;
            }
        }
        return std::nullopt;
    }

} // namespace HIKARI::EDITOR
