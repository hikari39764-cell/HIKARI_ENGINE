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
bool DocumentSceneEditorController::HasForegroundDocument(EDITOR::EditorWorkspaceId workspace) const {
    using W = EDITOR::EditorWorkspaceId;
    switch (workspace) {
    case W::Vfx: return vfxWorkspaceController_.GetDocument().IsOpen();
    case W::Cinematics: return cinematicsWorkspaceController_.IsEditingSequenceAsset() || cinematicsWorkspaceController_.IsSequenceDocumentDirty();
    case W::ModelMaterial: return modelMaterialWorkspaceController_.GetDocumentAssetGuid().IsValid() || modelMaterialWorkspaceController_.HasUnsavedMaterialChanges();
    case W::ModelCollision: return modelCollisionWorkspaceController_.IsEditingModel();
    case W::AnimationStateMachine: return animationStateMachineWorkspaceController_.IsDocumentOpen();
    case W::Dialogue: return dialogueWorkspaceController_.IsDocumentOpen();
    case W::RuntimeBinding: return runtimeBindingWorkspaceController_.IsDocumentOpen();
    default: return true;
    }
}
void DocumentSceneEditorController::DrawGlobalShell(DocumentSceneBase& scene) {
    using namespace EDITOR;
    EditorShellContents contents;
    if (projectWorkspaceHost_) contents.projectWorkspaces = [&](const char* filter) {
        projectWorkspaceHost_->DrawWorkspaceEntries(workspaceHost_, filter);
    };
    contents.documents = [&] {
        const auto active = workspaceHost_.GetActive();
        const std::string workspace(EditorWorkspaceOperationId(active));
        const auto documents = authoringSessions_.WorkspaceDocuments(workspace);
        if (documents.empty()) return;
        if (!ImGui::IsAnyItemActive() && !authoringSessions_.HasInteractiveEdit(authoringSessions_.VisibleSession(workspace))) {
            const auto previous = authoringSessions_.VisibleSession(workspace);
            if (authoringSessions_.ActivateDocument(workspace, {}, HasForegroundDocument(active)) &&
                previous != authoringSessions_.VisibleSession(workspace))
                authoringSessions_.Execute("authoring.reveal", {{"sessionId",authoringSessions_.VisibleSession(workspace)}});
        }
        const float scale = ImGui::GetFontSize() / 16.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{6*scale,3*scale});
        if (ImGui::BeginViewportSideBar("###Editor/Documents",ImGui::GetMainViewport(),ImGuiDir_Up,34*scale,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
            const auto visible = authoringSessions_.VisibleSession(workspace);
            if (IconTextButton(EditorGlyph::File,TEXT::LocalizeCStr("ai.document.manual"),"manualDocument",
                visible.empty() ? EditorButtonTone::Primary : EditorButtonTone::Quiet)) {
                authoringSessions_.ShowForeground(workspace);
                EditorWorkspaceOpenRequest request; request.workspaceId = active; workspaceHost_.RequestOpen(request);
            }
            ImGui::SameLine();
            bool follow = authoringSessions_.FollowDocuments();
            if (IconButton(EditorGlyph::Reveal,"followDocuments",follow ? EditorButtonTone::Primary : EditorButtonTone::Quiet,{},TEXT::LocalizeCStr("ai.document.follow")))
                authoringSessions_.SetFollowDocuments(!follow);
            if (aiAssistantSession_.IsBusy() && !visible.empty()) {
                ImGui::SameLine();
                if (IconButton(EditorGlyph::Pause,"takeOverDocument",EditorButtonTone::Quiet,{},TEXT::LocalizeCStr("ai.document.takeover"))) aiAssistantSession_.Cancel();
            }
            ImGui::SameLine();
            if (ImGui::BeginTabBar("LiveDocuments", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
                for (const auto& document : documents) {
                    const auto id = document.value("sessionId",std::string{});
                    auto label = document.value("name",workspace);
                    if (document.value("dirty",false)) label += " *";
                    label += "###" + id;
                    const bool shown = ImGui::BeginTabItem(label.c_str(),nullptr,
                        visible == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
                    // 自動選択を操作に変換しない。手動文書表示もそのまま保つ。
                    if (ImGui::IsItemClicked() && visible != id)
                        authoringSessions_.Execute("authoring.reveal", {{"sessionId",id}});
                    ImGui::SetItemTooltip("%s",TEXT::LocalizeCStr("ai.document.live"));
                    if (shown) ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End(); ImGui::PopStyleVar();
    };
    contents.assistantBusy = aiAssistantSession_.IsBusy();
    contents.tasksBusy = exportJob_.IsRunning() || projectCodeJob_.Running() || scene.GetAssetDatabase().GetAssetTaskService().HasActiveTasks();
    contents.assistant = [&] { aiAssistantPanel_.DrawEmbedded(); };
    contents.settings = [&] { editorSettingsPanel_.DrawContents(context_, [&] { aiAssistantPanel_.DrawPreferences(); }); };
    contents.resources = [&] {
        const ResourceWorkspaceContext resourceContext{
            scene.GetCurrentSceneAssetGuid(), context_.sceneDirty || scene.HasUnsavedSceneChanges(),
            scene.GetSceneDocumentRevision(), documentHistory_.GetEditRevision()};
        resourceWorkspacePanel_.DrawContents(scene.GetAssetDatabase(),scene.GetAssetRegistry(),scene.GetSceneDocument(),context_.selection,resourceContext);
    };
    contents.tasks = [&] {
        const auto job = [&](const char* key, bool active, const nlohmann::json& status, const std::function<void()>& cancel) {
            ImGui::PushID(key);
            ImGui::SeparatorText(TEXT::Localize(key).c_str());
            const bool completed=status.contains("success") && !active;
            StatusBadge(TEXT::Localize(active ? "shell.busy" : completed ? (status.value("success",false) ? "shell.completed" : "shell.failed") : "shell.ready").c_str(),
                active ? EditorStatusTone::Normal : completed && !status.value("success",false) ? EditorStatusTone::Error : EditorStatusTone::Ready);
            if (status.contains("message")) ImGui::TextWrapped("%s",status.value("message",std::string{}).c_str());
            if (active) {
                if(status.contains("progress")) ImGui::ProgressBar(status.value("progress",0.0f));
                if(IconTextButton(EditorGlyph::Stop,TEXT::Localize("shell.cancelTask").c_str(),"cancel")) cancel();
            }
            if(status.contains("logDirectory") && IconTextButton(EditorGlyph::Log,TEXT::Localize("log.openFile").c_str(),"logs"))
                SHELL::OpenFolderInExplorer(TEXT::PathFromUtf8(status.value("logDirectory",std::string{})));
            ImGui::PopID();
        };
        job("shell.export",exportJob_.IsRunning(),exportJob_.Status(),[&]{exportJob_.Cancel();});
        job("shell.projectBuild",projectCodeJob_.Running(),projectCodeJob_.Status(),[&]{projectCodeJob_.Cancel();});
        ImGui::SeparatorText(TEXT::Localize("shell.import").c_str());
        auto& assetTasks = scene.GetAssetDatabase().GetAssetTaskService();
        auto snapshots = assetTasks.CollectSnapshots();
        std::sort(snapshots.begin(), snapshots.end(), [](const auto& a, const auto& b) { return a.id > b.id; });
        if (snapshots.empty()) ImGui::TextDisabled("%s", TEXT::LocalizeCStr("shell.ready"));
        for (const auto& task : snapshots) {
            ImGui::PushID(static_cast<int>(task.id));
            const bool active = !IsTerminal(task.state);
            const char* state = active ? "shell.busy" : task.state == AssetTaskState::Failed ? "shell.failed" :
                task.state == AssetTaskState::Canceled ? "shell.cancelled" : "shell.completed";
            StatusBadge(TEXT::LocalizeCStr(state), task.state == AssetTaskState::Failed ? EditorStatusTone::Error : EditorStatusTone::Normal);
            ImGui::TextWrapped("%s", task.label.c_str());
            if (active) {
                ImGui::TextWrapped("%s", task.progress.stage.c_str());
                if (task.progress.determinate) ImGui::ProgressBar(task.progress.normalized);
                ImGui::BeginDisabled(!task.cancelable || task.cancellationRequested);
                if (IconTextButton(EditorGlyph::Stop, TEXT::LocalizeCStr("shell.cancelTask"), "cancelAssetTask"))
                    assetTasks.RequestCancel(task.id);
                ImGui::EndDisabled();
            } else if (!task.resultMessage.empty()) ImGui::TextWrapped("%s", task.resultMessage.c_str());
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::SeparatorText(TEXT::Localize("shell.ai").c_str());
        ImGui::TextUnformatted(TEXT::Localize(contents.assistantBusy ? "shell.busy" : "shell.ready").c_str());
        if (contents.assistantBusy) {
            const auto activity=aiAssistantSession_.SnapshotActivity();
            ImGui::TextWrapped("%s",activity.action.c_str());
            if(IconTextButton(EditorGlyph::Stop,TEXT::Localize("shell.cancelTask").c_str(),"stopAssistant")) aiAssistantSession_.Cancel();
        }
    };
    editorShell_.Draw(context_, workspaceHost_, commandRouter_, contents);
}
}
