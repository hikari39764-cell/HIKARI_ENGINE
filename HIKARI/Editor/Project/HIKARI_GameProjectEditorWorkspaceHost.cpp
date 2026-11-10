#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceUi.h"
#include "Editor/Project/HIKARI_GameProjectEditorDocumentService.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR {

GameProjectEditorWorkspaceHost::GameProjectEditorWorkspaceHost()
    : ui_(std::make_unique<GameProjectEditorWorkspaceUi>()) {}

GameProjectEditorWorkspaceHost::~GameProjectEditorWorkspaceHost() = default;

bool GameProjectEditorWorkspaceHost::RegisterModule(const GameProjectEditorModuleLibrary& module,
                                                    std::string_view displayName, std::string_view projectRootUtf8,
                                                    EditorOperationRegistry& operationRegistry,
                                                    std::string& outMessage) {
    if (!module.IsLoaded()) {
        outMessage.clear();
        return true;
    }
    const EditorOperationOwnerToken operationOwnerToken = operationRegistry.CreateOwnerToken();
    if (operationOwnerToken == 0u) {
        outMessage = "Could not allocate an Editor operation owner token for the GameProject module.";
        return false;
    }
    // 文書サービスは項目に一つ。二つ目以降のモジュールは最初の登録で作ったものを共有する。
    const bool firstModule = documents_ == nullptr;
    if (firstModule) {
        documents_ = std::make_unique<GameProjectEditorDocumentService>(TEXT::PathFromUtf8(projectRootUtf8) / "Data");
        registry_.SetDocuments(documents_.get());
    }
    const auto discardDocumentsOfFirstModule = [&]() {
        if (firstModule) {
            registry_.SetDocuments(nullptr);
            documents_.reset();
        }
    };
    std::string operationMessage{};
    if (!module.RegisterEditorOperations(operationRegistry, operationOwnerToken, documents_.get(), &extensions_,
                                         operationMessage, &registry_)) {
        (void)operationRegistry.UnregisterOwner(operationOwnerToken);
        discardDocumentsOfFirstModule();
        outMessage = "GameProject Editor operation registration failed: " + operationMessage;
        return false;
    }

    registry_.SetProjectRootUtf8(std::string(projectRootUtf8));
    std::vector<EDITOR_PROJECT::IEditorWorkspace*> workspaces{};
    workspaces.reserve(module.GetWorkspaceCount());
    for (size_t index = 0u; index < module.GetWorkspaceCount(); ++index) {
        workspaces.push_back(module.GetWorkspace(index));
    }
    const bool registered =
        registry_.RegisterModule(module.GetModuleId(), workspaces.data(), workspaces.size(), outMessage);
    if (registered) {
        operationRegistry_ = &operationRegistry;
        operationOwnerTokens_.push_back(operationOwnerToken);
        extensions_.Add(module);
        if (!displayName.empty()) {
            moduleNames_.emplace_back(module.GetModuleId(), displayName);
        }
    } else {
        (void)operationRegistry.UnregisterOwner(operationOwnerToken);
        discardDocumentsOfFirstModule();
    }
    return registered;
}

void GameProjectEditorWorkspaceHost::DrawModulePanels(AssetDatabase& database, AssetRegistry& assets) {
    if (documents_) {
        documents_->SetAssetDatabase(&database);
    }
    if (extensions_.GetPanels().empty()) {
        return;
    }
    ui_->SetAssets(database, assets);
    ui_->BeginFrame("scene");
    ui_->ClearDockRegions();
    extensions_.DrawPanels(*ui_);
    ui_->EndFrame();
}

bool GameProjectEditorWorkspaceHost::RequestOpenAsset(std::string_view moduleAssetTypeId, std::string_view assetGuid) {
    const std::string_view workspaceId = registry_.FindWorkspaceForAssetType(moduleAssetTypeId);
    if (workspaceId.empty()) {
        return false;
    }
    registry_.SetPendingAsset(workspaceId, assetGuid);
    RequestWorkspace(std::string(workspaceId));
    return true;
}

void GameProjectEditorWorkspaceHost::ReportModuleFailure(std::string message) {
    moduleFailureMessage_ = std::move(message);
}

void GameProjectEditorWorkspaceHost::UpdateNavigation(EditorWorkspaceHost& host, float unscaledDeltaSeconds) {
    if (!IsAvailable())
        return;
    // モジュールが自分の工作区を開くよう頼んだ時（操作の本体からの RequestActivateWorkspace など）、項目の工作区の
    // 画面が前に出ていなければ、資産を開く時と同じ道で先に画面を前へ出す。
    if (!host.IsActive(EditorWorkspaceId::Project)) {
        if (std::string requested = registry_.TakePendingActivation(); !requested.empty()) {
            requestedWorkspace_ = std::move(requested);
        }
    }
    if (!requestedWorkspace_.empty() && !host.IsActive(EditorWorkspaceId::Project))
        (void)host.RequestOpen({EditorWorkspaceId::Project});
    ui_->BeginFrame(registry_.GetActiveWorkspaceId());
    registry_.BeginFrame(*ui_, unscaledDeltaSeconds);
    if (host.IsActive(EditorWorkspaceId::Project)) {
        if (const auto* pending = host.PeekPending(); pending && pending->workspaceId != EditorWorkspaceId::Project) {
            guardedNavigation_ = *pending;
            host.CancelPending();
            (void)registry_.RequestCloseActiveWorkspace();
        }
        if (!requestedWorkspace_.empty()) {
            (void)registry_.RequestActivateWorkspace(requestedWorkspace_);
            requestedWorkspace_.clear();
        }
    }
    registry_.ProcessPending();
    const bool wasAwaiting = registry_.IsAwaitingCloseResolution();
    DrawClosePrompt();
    if (guardedNavigation_ && !registry_.HasActiveWorkspace()) {
        (void)host.RequestOpen(*guardedNavigation_);
        guardedNavigation_.reset();
    } else if (wasAwaiting && !registry_.IsAwaitingCloseResolution()) {
        guardedNavigation_.reset();
    }
    registry_.EndFrame();
    ui_->EndFrame();
}

void GameProjectEditorWorkspaceHost::Draw(EditorWorkspaceHost& host, float unscaledDeltaSeconds,
                                          AssetDatabase& database, AssetRegistry& assets) {
    if (!IsAvailable())
        return;
    if (documents_) {
        documents_->SetAssetDatabase(&database);
    }
    ui_->SetAssets(database, assets);
    ui_->BeginFrame(registry_.GetActiveWorkspaceId());
    registry_.BeginFrame(*ui_, unscaledDeltaSeconds);
    if (!requestedWorkspace_.empty()) {
        (void)registry_.RequestActivateWorkspace(requestedWorkspace_);
        requestedWorkspace_.clear();
    }
    registry_.ProcessPending();
    ui_->SetWorkspaceId(registry_.GetActiveWorkspaceId());
    ui_->DrawDockSpace(resetLayout_ || host.ConsumeReset(EditorWorkspaceId::Project),
                       ActiveWorkspaceRequestsSceneViewport());
    resetLayout_ = false;
    DrawToolbar(host);
    DrawClosePrompt();
    ui_->SetHistoryRequest(undoRequested_, redoRequested_);
    undoRequested_ = false;
    redoRequested_ = false;
    registry_.DrawActive();
    ui_->SetHistoryRequest(false, false);
    registry_.EndFrame();
    ui_->EndFrame();
    if (!registry_.HasActiveWorkspace() && !registry_.IsAwaitingCloseResolution()) {
        (void)host.RequestOpen({EditorWorkspaceId::Scene});
    }
}

void GameProjectEditorWorkspaceHost::Shutdown() noexcept {
    if (ui_) {
        ui_->BeginFrame(registry_.GetActiveWorkspaceId());
        registry_.Shutdown(*ui_);
        ui_->EndFrame();
    }
    if (operationRegistry_ != nullptr) {
        for (const uint64_t token : operationOwnerTokens_) {
            (void)operationRegistry_->UnregisterOwner(token);
        }
    }
    registry_.SetDocuments(nullptr);
    if (documents_) {
        documents_->CloseAll();
        documents_.reset();
    }
    operationRegistry_ = nullptr;
    operationOwnerTokens_.clear();
    (void)extensions_.ActivateTool({});
    extensions_.Clear();
    moduleNames_.clear();
    moduleFailureMessage_.clear();
    closePromptWasPending_ = false;
}

bool GameProjectEditorWorkspaceHost::IsAvailable() const noexcept {
    return registry_.GetWorkspaceCount() != 0u || !moduleFailureMessage_.empty();
}

bool GameProjectEditorWorkspaceHost::ActiveWorkspaceRequestsSceneViewport() const noexcept {
    return registry_.ActiveWorkspaceRequestsSceneViewport();
}

bool GameProjectEditorWorkspaceHost::HasUnsavedChanges() const noexcept {
    return (documents_ != nullptr && documents_->HasUnsavedDocuments()) || registry_.HasUnsavedWorkspaceChanges();
}

std::string GameProjectEditorWorkspaceHost::PrepareSceneViewportWindow() {
    if (ui_ && ActiveWorkspaceRequestsSceneViewport()) {
        ui_->PrepareSceneViewportWindow();
    }
    return "Project Scene View/" + std::string(registry_.GetActiveWorkspaceId());
}

GameProjectEditorWorkspaceRegistry& GameProjectEditorWorkspaceHost::Registry() noexcept { return registry_; }

const GameProjectEditorWorkspaceRegistry& GameProjectEditorWorkspaceHost::Registry() const noexcept {
    return registry_;
}

// 挿件包のモジュールは「模块 名前」、項目自身のコードは「本项目」。
std::string GameProjectEditorWorkspaceHost::ModuleLabel(std::string_view moduleId) const {
    for (const auto& [id, name] : moduleNames_) {
        if (id == moduleId)
            return TEXT::Localize("shell.module") + "  " + name;
    }
    return TEXT::Localize("shell.projectModule");
}

void GameProjectEditorWorkspaceHost::ListWorkspaces(EditorWorkspaceHost& host, const char* filter,
                                                    const WorkspaceItem& drawItem) {
    std::string_view module{};
    for (size_t i = 0; i < registry_.GetWorkspaceCount(); ++i) {
        const auto info = registry_.GetWorkspaceInfo(i);
        if (filter && filter[0] && info.displayName.find(filter) == std::string_view::npos)
            continue;
        if (info.moduleId != module) {
            module = info.moduleId;
            ImGui::SeparatorText(ModuleLabel(module).c_str());
        }
        if (drawItem(info, info.active && host.IsActive(EditorWorkspaceId::Project))) {
            requestedWorkspace_.assign(info.workspaceId);
            (void)host.RequestOpen({EditorWorkspaceId::Project});
        }
    }
}

void GameProjectEditorWorkspaceHost::DrawWorkspaceEntries(EditorWorkspaceHost& host, const char* filter) {
    if (!IsAvailable())
        return;
    ListWorkspaces(host, filter, [](const GameProjectEditorWorkspaceInfo& info, bool active) {
        ImGui::BeginDisabled(info.disabled);
        const bool clicked = IconTextButton(EditorGlyph::Grid, std::string(info.displayName).c_str(),
                                            std::string(info.workspaceId).c_str(),
                                            active ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {-1, 36});
        ImGui::EndDisabled();
        return clicked;
    });
    if (!moduleFailureMessage_.empty())
        ImGui::TextWrapped("%s", moduleFailureMessage_.c_str());
}

void GameProjectEditorWorkspaceHost::DrawWorkspaceMenuItems(EditorWorkspaceHost& host) {
    if (!IsAvailable())
        return;
    ListWorkspaces(host, nullptr, [](const GameProjectEditorWorkspaceInfo& info, bool active) {
        const std::string label = std::string(info.displayName) + "###" + std::string(info.workspaceId);
        return ImGui::MenuItem(label.c_str(), nullptr, active, !info.disabled);
    });
}

// 作業区の工具栏は一つ。題は今の作業区の名前で、宿主の一行（場景へ戻る・保存・配置の初期化・どのモジュールの物か・
// 状態）のあとへ、モジュールが Toolbar 区画へ出す内容が続く。
void GameProjectEditorWorkspaceHost::DrawToolbar(EditorWorkspaceHost& host) {
    GameProjectEditorWorkspaceInfo active{};
    for (size_t i = 0; i < registry_.GetWorkspaceCount() && !active.active; ++i)
        active = registry_.GetWorkspaceInfo(i);
    if (ui_->BeginToolbar(active.active ? active.displayName : std::string_view{})) {
        if (IconButton(EditorGlyph::Scene, "backToScene", EditorButtonTone::Quiet, {},
                       TEXT::Localize("menu.workspace.scene").c_str()))
            (void)host.RequestOpen({EditorWorkspaceId::Scene});
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Save, "saveProjectWorkspace", EditorButtonTone::Quiet, {},
                       TEXT::Localize("menu.edit.save").c_str()))
            (void)registry_.RequestSaveActiveWorkspace();
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Refresh, "resetProjectLayout", EditorButtonTone::Quiet, {},
                       TEXT::Localize("shell.resetLayout").c_str()))
            resetLayout_ = true;
        if (active.active) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ModuleLabel(active.moduleId).c_str());
        }
        if (registry_.IsActiveWorkspaceDirty()) {
            ImGui::SameLine();
            StatusBadge(TEXT::Localize("shell.unsaved").c_str(), EditorStatusTone::Warning);
        }
        if (!registry_.GetStatusMessage().empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(registry_.GetStatusMessage().data(),
                                   registry_.GetStatusMessage().data() + registry_.GetStatusMessage().size());
        }
    }
    ui_->EndPanel();
}

void GameProjectEditorWorkspaceHost::DrawClosePrompt() {
    const bool pending = registry_.IsAwaitingCloseResolution();
    if (pending && !closePromptWasPending_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("shell.unsaved", "Unsaved Project Workspace").c_str());
    }
    closePromptWasPending_ = pending;
    if (!pending) {
        return;
    }

    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("shell.unsaved", "Unsaved Project Workspace").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(TEXT::Localize("shell.saveBeforeLeaving").c_str(),
                           std::string(registry_.GetAwaitingWorkspaceName()).c_str());
        ImGui::Separator();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.save", "Save").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Save);
            if (!registry_.IsAwaitingCloseResolution()) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.sceneOpen.discard", "Discard").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Discard);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Cancel);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace HIKARI::EDITOR
