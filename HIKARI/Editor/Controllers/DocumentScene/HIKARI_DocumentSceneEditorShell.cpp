#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI {
nlohmann::json DocumentSceneEditorController::DescribeWorkspaceDocument(EDITOR::EditorWorkspaceId workspace,
                                                                        bool includeSessions) const {
    using W = EDITOR::EditorWorkspaceId;
    const auto describe = [includeSessions](const auto& visible, const auto& foreground) {
        return (includeSessions ? visible : foreground).DescribeDocumentState();
    };
    switch (workspace) {
    case W::Text: return textWorkspaceController_.DescribeDocumentState();
    case W::GameplayCamera: return describe(GameplayCameraWorkspace(), gameplayCameraWorkspaceController_);
    case W::Vfx: return describe(VfxWorkspace(), vfxWorkspaceController_);
    case W::Cinematics: return describe(CinematicsWorkspace(), cinematicsWorkspaceController_);
    case W::ModelMaterial: return describe(ModelMaterialWorkspace(), modelMaterialWorkspaceController_);
    case W::ModelCollision: return describe(ModelCollisionWorkspace(), modelCollisionWorkspaceController_);
    case W::AnimationStateMachine:
        return describe(AnimationStateMachineWorkspace(), animationStateMachineWorkspaceController_);
    case W::Dialogue: return describe(DialogueWorkspace(), dialogueWorkspaceController_);
    case W::RuntimeBinding: return describe(RuntimeBindingWorkspace(), runtimeBindingWorkspaceController_);
    case W::LookDev: return DescribeLookDevState();
    default: return nullptr;
    }
}
bool DocumentSceneEditorController::HasForegroundDocument(EDITOR::EditorWorkspaceId workspace) const {
    // 毎フレーム呼ばれるため、文書状態の JSON は組み立てない。
    using W = EDITOR::EditorWorkspaceId;
    switch (workspace) {
    case W::Text: return textWorkspaceController_.IsDocumentOpen();
    case W::GameplayCamera: return gameplayCameraWorkspaceController_.IsDocumentOpen();
    case W::Vfx: return vfxWorkspaceController_.IsDocumentOpen();
    case W::Cinematics: return cinematicsWorkspaceController_.IsDocumentOpen();
    case W::ModelMaterial: return modelMaterialWorkspaceController_.IsDocumentOpen();
    case W::ModelCollision: return modelCollisionWorkspaceController_.IsDocumentOpen();
    case W::AnimationStateMachine: return animationStateMachineWorkspaceController_.IsDocumentOpen();
    case W::Dialogue: return dialogueWorkspaceController_.IsDocumentOpen();
    case W::RuntimeBinding: return runtimeBindingWorkspaceController_.IsDocumentOpen();
    default: return true;
    }
}
void DocumentSceneEditorController::DrawGlobalShell(DocumentSceneBase& scene) {
    using namespace EDITOR;
    EditorShellContents contents;
    if (projectWorkspaceHost_)
        contents.projectWorkspaces = [&](const char* filter) {
            projectWorkspaceHost_->DrawWorkspaceEntries(workspaceHost_, filter);
        };
    contents.documents = [&] {
        const auto active = workspaceHost_.GetActive();
        const std::string workspace(EditorWorkspaceOperationId(active));
        const auto documents = authoringSessions_.WorkspaceDocuments(workspace);
        if (documents.empty())
            return;
        if (!ImGui::IsAnyItemActive() &&
            !authoringSessions_.HasInteractiveEdit(authoringSessions_.VisibleSession(workspace))) {
            const auto previous = authoringSessions_.VisibleSession(workspace);
            if (authoringSessions_.ActivateDocument(workspace, {}, HasForegroundDocument(active)) &&
                previous != authoringSessions_.VisibleSession(workspace))
                authoringSessions_.Execute("authoring.reveal",
                                           {{"sessionId", authoringSessions_.VisibleSession(workspace)}});
        }
        const float scale = ImGui::GetFontSize() / 16.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6 * scale, 3 * scale});
        if (ImGui::BeginViewportSideBar("###Editor/Documents", ImGui::GetMainViewport(), ImGuiDir_Up, 34 * scale,
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
            const auto visible = authoringSessions_.VisibleSession(workspace);
            if (IconTextButton(EditorGlyph::File, TEXT::Localize("authoring.document.manual").c_str(), "manualDocument",
                               visible.empty() ? EditorButtonTone::Primary : EditorButtonTone::Quiet)) {
                authoringSessions_.ShowForeground(workspace);
                EditorWorkspaceOpenRequest request;
                request.workspaceId = active;
                workspaceHost_.RequestOpen(request);
            }
            ImGui::SameLine();
            bool follow = authoringSessions_.FollowDocuments();
            if (IconButton(EditorGlyph::Reveal, "followDocuments",
                           follow ? EditorButtonTone::Primary : EditorButtonTone::Quiet, {},
                           TEXT::Localize("authoring.document.follow").c_str()))
                authoringSessions_.SetFollowDocuments(!follow);
            if (agentPanel_.turnActive && !visible.empty()) {
                ImGui::SameLine();
                if (IconButton(EditorGlyph::Pause, "takeOverDocument", EditorButtonTone::Quiet, {},
                               TEXT::Localize("authoring.document.takeover").c_str()))
                    agentPanel_.session.Interrupt();
            }
            ImGui::SameLine();
            if (ImGui::BeginTabBar("LiveDocuments",
                                   ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
                for (const auto& document : documents) {
                    const auto id = document.value("sessionId", std::string{});
                    auto label = document.value("name", workspace);
                    if (document.value("dirty", false))
                        label += " *";
                    label += "###" + id;
                    const bool shown = ImGui::BeginTabItem(
                        label.c_str(), nullptr, visible == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
                    // 自動選択を操作に変換しない。手動文書表示もそのまま保つ。
                    if (ImGui::IsItemClicked() && visible != id)
                        authoringSessions_.Execute("authoring.reveal", {{"sessionId", id}});
                    ImGui::SetItemTooltip("%s", TEXT::Localize("authoring.document.live").c_str());
                    if (shown)
                        ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    };
    contents.assistantBusy = agentPanel_.turnActive;
    contents.assistantAttention = agentHost_.HasPendingApprovals();
    contents.tasksBusy = exportJob_.IsRunning() || projectCodeJob_.Running() ||
                         scene.GetAssetDatabase().GetAssetTaskService().HasActiveTasks();
    contents.unsavedSummary = UnsavedDocumentSummary(scene);
    contents.assistant = [&] { DrawAgentPanel(scene); };
    contents.settings = [&] {
        editorSettingsPanel_.DrawContents(context_, [&] {
            if (ImGui::Button(TEXT::Localize("settings.aiBridge.writeAgentFiles").c_str()))
                agentFilesMessage_ = operationRegistry_.Execute("project.agent_files.write").message;
            if (!agentFilesMessage_.empty())
                ImGui::TextWrapped("%s", agentFilesMessage_.c_str());
        });
    };
    contents.resources = [&] {
        const ResourceWorkspaceContext resourceContext{
            scene.GetCurrentSceneAssetGuid(), context_.sceneDirty || scene.HasUnsavedSceneChanges(),
            scene.GetSceneDocumentRevision(), documentHistory_.GetEditRevision()};
        resourceWorkspacePanel_.DrawContents(scene.GetAssetDatabase(), scene.GetAssetRegistry(),
                                             scene.GetSceneDocument(), context_.selection, resourceContext);
    };
    contents.tasks = [&] {
        const auto job = [&](const char* key, bool active, const nlohmann::json& status,
                             const std::function<void()>& cancel) {
            ImGui::PushID(key);
            ImGui::SeparatorText(TEXT::Localize(key).c_str());
            const bool completed = status.contains("success") && !active;
            StatusBadge(TEXT::Localize(active ? "shell.busy"
                                       : completed
                                           ? (status.value("success", false) ? "shell.completed" : "shell.failed")
                                           : "shell.ready")
                            .c_str(),
                        active                                         ? EditorStatusTone::Normal
                        : completed && !status.value("success", false) ? EditorStatusTone::Error
                                                                       : EditorStatusTone::Ready);
            if (status.contains("message"))
                ImGui::TextWrapped("%s", status.value("message", std::string{}).c_str());
            if (active) {
                if (status.contains("progress"))
                    ImGui::ProgressBar(status.value("progress", 0.0f));
                if (IconTextButton(EditorGlyph::Stop, TEXT::Localize("shell.cancelTask").c_str(), "cancel"))
                    cancel();
            }
            if (status.contains("logDirectory") &&
                IconTextButton(EditorGlyph::Log, TEXT::Localize("log.openFile").c_str(), "logs"))
                SHELL::OpenFolderInExplorer(TEXT::PathFromUtf8(status.value("logDirectory", std::string{})));
            ImGui::PopID();
        };
        job("shell.export", exportJob_.IsRunning(), exportJob_.Status(), [&] { exportJob_.Cancel(); });
        job("shell.projectBuild", projectCodeJob_.Running(), projectCodeJob_.Status(),
            [&] { projectCodeJob_.Cancel(); });
        ImGui::SeparatorText(TEXT::Localize("shell.import").c_str());
        auto& assetTasks = scene.GetAssetDatabase().GetAssetTaskService();
        auto snapshots = assetTasks.CollectSnapshots();
        std::sort(snapshots.begin(), snapshots.end(), [](const auto& a, const auto& b) { return a.id > b.id; });
        if (snapshots.empty())
            ImGui::TextDisabled("%s", TEXT::Localize("shell.ready").c_str());
        for (const auto& task : snapshots) {
            ImGui::PushID(static_cast<int>(task.id));
            const bool active = !IsTerminal(task.state);
            const char* state = active                                   ? "shell.busy"
                                : task.state == AssetTaskState::Failed   ? "shell.failed"
                                : task.state == AssetTaskState::Canceled ? "shell.cancelled"
                                                                         : "shell.completed";
            StatusBadge(TEXT::Localize(state).c_str(),
                        task.state == AssetTaskState::Failed ? EditorStatusTone::Error : EditorStatusTone::Normal);
            ImGui::TextWrapped("%s", task.label.c_str());
            if (active) {
                ImGui::TextWrapped("%s", task.progress.stage.c_str());
                if (task.progress.determinate)
                    ImGui::ProgressBar(task.progress.normalized);
                ImGui::BeginDisabled(!task.cancelable || task.cancellationRequested);
                if (IconTextButton(EditorGlyph::Stop, TEXT::Localize("shell.cancelTask").c_str(), "cancelAssetTask"))
                    assetTasks.RequestCancel(task.id);
                ImGui::EndDisabled();
            } else if (!task.resultMessage.empty())
                ImGui::TextWrapped("%s", task.resultMessage.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::SeparatorText(TEXT::Localize("shell.ai").c_str());
        ImGui::TextUnformatted(TEXT::Localize(contents.assistantBusy ? "shell.busy" : "shell.ready").c_str());
        if (contents.assistantBusy &&
            IconTextButton(EditorGlyph::Stop, TEXT::Localize("shell.cancelTask").c_str(), "stopAssistant"))
            agentPanel_.session.Interrupt();
    };
    editorShell_.Draw(context_, workspaceHost_, commandRouter_, contents);
}
} // namespace HIKARI
