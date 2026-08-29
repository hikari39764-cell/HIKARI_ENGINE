#include "Editor/App/HIKARI_ProjectLauncherApp.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Editor/Style/HIKARI_EditorFontManager.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Project/HIKARI_ProjectDescriptor.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

    template <size_t Size>
    void CopyTextToBuffer(
        const std::string& text,
        std::array<char, Size>& buffer) {

        static_assert(Size > 0u);
        const size_t characterCount =
            (std::min)(text.size(), Size - 1u);
        std::memcpy(
            buffer.data(),
            text.data(),
            characterCount);
        buffer[characterCount] = '\0';
    }

    void BeginPanelStyle() {
        const EditorThemePalette& palette =
            GetEditorThemePalette();
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            palette.panel);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            GetEditorThemeMetrics().panelRounding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize,
            1.0f);
    }

    void EndPanelStyle() {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

} // namespace

    ProjectLauncherApp::ProjectLauncherApp(
        std::filesystem::path runtimeRoot,
        ProjectLauncherStartupRequest startup)
        : runtimeRoot_(
            std::filesystem::absolute(
                std::move(runtimeRoot))
                .lexically_normal()),
          startup_(std::move(startup)),
          openWorkflow_(runtimeRoot_) {
    }

    bool ProjectLauncherApp::Initialize() {
        std::string recentMessage{};
        if (!recentProjects_.Load(&recentMessage)) {
            interactionMessage_ =
                std::move(recentMessage);
        }

        const std::filesystem::path defaultParent =
            GetDefaultProjectParentDirectory();
        CopyTextToBuffer(
            TEXT::PathToUtf8(defaultParent),
            projectLocation_);

        const bool rebuildWaitInvalid =
            startup_.rebuildProject.has_value() &&
            (!startup_.waitForProcessId.has_value() ||
                *startup_.waitForProcessId == 0u);
        if (startup_.waitForProcessIdMalformed ||
            rebuildWaitInvalid) {
            interactionMessage_ =
                "The rebuild handoff process ID is invalid; "
                "the build was not started.";
        }
        else if (startup_.rebuildProject) {
            std::string rebuildMessage{};
            const std::filesystem::path descriptorPath =
                ResolveProjectDescriptorPath(
                    *startup_.rebuildProject);
            const std::filesystem::path projectRoot =
                descriptorPath.parent_path();
            const bool started =
                openWorkflow_.StartRebuildAfterProcessExit(
                    projectRoot,
                    *startup_.waitForProcessId,
                    rebuildMessage);
            if (!started) {
                interactionMessage_ =
                    std::move(rebuildMessage);
            }
            else {
                std::string touchMessage{};
                if (!recentProjects_.AddOrTouch(
                        projectRoot,
                        &touchMessage)) {
                    interactionMessage_ =
                        std::move(touchMessage);
                }
            }
        }
        return true;
    }

    void ProjectLauncherApp::Update(float) {
        openWorkflow_.Update();
    }

    void ProjectLauncherApp::Render() {
    }

    void ProjectLauncherApp::RenderHostUi() {
        const ImGuiViewport* viewport =
            ImGui::GetMainViewport();
        if (viewport == nullptr) {
            return;
        }

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowRounding,
            0.0f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            0.0f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoDocking;
        const bool visible = ImGui::Begin(
            "HIKARI Project Hub",
            nullptr,
            windowFlags);
        ImGui::PopStyleVar(3);
        if (!visible) {
            ImGui::End();
            return;
        }

        DrawHeader();

        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        const float availableWidth =
            ImGui::GetContentRegionAvail().x;
        const float contentWidth =
            (std::min)(availableWidth, 1120.0f);
        ImGui::SetCursorPosX(
            (availableWidth - contentWidth) * 0.5f);
        ImGui::BeginGroup();
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingLg));

        const float quickStartWidth =
            (std::min)(360.0f, contentWidth * 0.38f);
        const float panelHeight =
            (std::max)(
                260.0f,
                ImGui::GetContentRegionAvail().y -
                    76.0f);
        BeginPanelStyle();
        if (ImGui::BeginChild(
                "##ProjectQuickStart",
                ImVec2(quickStartWidth, panelHeight),
                true)) {
            DrawQuickStartPanel();
        }
        ImGui::EndChild();
        EndPanelStyle();

        ImGui::SameLine(
            0.0f,
            metrics.spacingLg);
        BeginPanelStyle();
        if (ImGui::BeginChild(
                "##RecentProjects",
                ImVec2(0.0f, panelHeight),
                true)) {
            DrawRecentProjectsPanel();
        }
        ImGui::EndChild();
        EndPanelStyle();

        DrawStatusPanel();
        ImGui::EndGroup();

        DrawCreateProjectDialog();
        ImGui::End();
    }

    bool ProjectLauncherApp::
        ShouldExitHost() const {
        return openWorkflow_.ShouldExitLauncher();
    }

    void ProjectLauncherApp::Shutdown() {
        openWorkflow_.Shutdown();
    }

    void ProjectLauncherApp::DrawHeader() {
        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        const EditorThemePalette& palette =
            GetEditorThemePalette();

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            palette.panel);
        if (ImGui::BeginChild(
                "##ProjectHubHeader",
                ImVec2(0.0f, 72.0f),
                false,
                ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SetCursorPos(
                ImVec2(
                    metrics.spacingLg * 1.5f,
                    metrics.spacingLg));
            const bool strongFont =
                PushEditorFont(
                    EditorFontRole::Strong);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                palette.accentHover);
            ImGui::TextUnformatted("HIKARI");
            ImGui::PopStyleColor();
            PopEditorFont(strongFont);

            ImGui::SameLine(
                0.0f,
                metrics.spacingMd);
            ImGui::SetCursorPosY(
                metrics.spacingLg + 1.0f);
            ImGui::TextDisabled("PROJECT HUB");
            ImGui::SetCursorPos(
                ImVec2(
                    metrics.spacingLg * 1.5f,
                    42.0f));
            ImGui::TextDisabled(
                "Create, build, and open game projects "
                "against one shared engine installation.");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProjectLauncherApp::DrawQuickStartPanel() {
        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        ImGui::SetCursorPos(
            ImVec2(
                metrics.spacingLg,
                metrics.spacingLg));
        ImGui::BeginGroup();
        PanelTitle(
            "Quick Start",
            "Choose a project workflow");
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingMd));

        const bool busy = openWorkflow_.IsBusy();
        if (busy) {
            ImGui::BeginDisabled();
        }
        if (IconTextButton(
                EditorGlyph::NewDocument,
                "Create Project",
                "CreateProject",
                EditorButtonTone::Primary,
                ImVec2(-1.0f, 42.0f),
                "Create a small external GameProject")) {
            interactionMessage_.clear();
            createDialogRequested_ = true;
        }
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingSm));
        if (IconTextButton(
                EditorGlyph::Folder,
                "Open Project",
                "OpenProject",
                EditorButtonTone::Neutral,
                ImVec2(-1.0f, 42.0f),
                "Select a folder containing Project.hikari")) {
            BrowseForProject();
        }
        if (busy) {
            ImGui::EndDisabled();
        }

        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingLg));
        ImGui::Separator();
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingMd));
        StatusBadge(
            "Shared engine build",
            EditorStatusTone::Ready);
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingSm));
        ImGui::PushTextWrapPos();
        ImGui::TextDisabled(
            "Projects keep their own Source, Assets, and "
            "ProjectSettings. Engine binaries and third-party "
            "packages remain in this HIKARI checkout.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    void ProjectLauncherApp::
        DrawRecentProjectsPanel() {
        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        const EditorThemePalette& palette =
            GetEditorThemePalette();
        ImGui::SetCursorPos(
            ImVec2(
                metrics.spacingLg,
                metrics.spacingLg));
        ImGui::BeginGroup();
        PanelTitle(
            "Recent Projects",
            "Projects opened on this machine");
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingMd));

        if (recentProjects_.GetEntries().empty()) {
            EmptyState(
                "No recent projects",
                "Create a project or open an existing "
                "Project.hikari folder.");
            ImGui::EndGroup();
            return;
        }

        const bool busy = openWorkflow_.IsBusy();
        const auto entries =
            recentProjects_.GetEntries();
        for (size_t index = 0u;
            index < entries.size();
            ++index) {
            const RecentProjectEntry& entry =
                entries[index];
            ImGui::PushID(
                static_cast<int>(index));
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                palette.raised);
            if (ImGui::BeginChild(
                    "##RecentProjectCard",
                    ImVec2(-1.0f, 82.0f),
                    true,
                    ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SetCursorPos(
                    ImVec2(
                        metrics.spacingMd,
                        metrics.spacingSm));
                const bool strongFont =
                    PushEditorFont(
                        EditorFontRole::Strong);
                ImGui::TextUnformatted(
                    entry.name.c_str());
                PopEditorFont(strongFont);

                ImGui::SetCursorPosX(
                    metrics.spacingMd);
                const float actionWidth = 126.0f;
                ImGui::PushTextWrapPos(
                    (std::max)(
                        metrics.spacingMd,
                        ImGui::GetWindowWidth() -
                            actionWidth));
                ImGui::TextDisabled(
                    "%s",
                    TEXT::PathToUtf8(
                        entry.root).c_str());
                ImGui::PopTextWrapPos();

                ImGui::SetCursorPos(
                    ImVec2(
                        ImGui::GetWindowWidth() -
                            actionWidth,
                        26.0f));
                if (busy) {
                    ImGui::BeginDisabled();
                }
                if (IconTextButton(
                        EditorGlyph::Folder,
                        "Open",
                        "OpenRecent",
                        EditorButtonTone::Primary,
                        ImVec2(76.0f, 30.0f))) {
                    OpenProject(entry.root);
                }
                ImGui::SameLine(
                    0.0f,
                    metrics.spacingXs);
                if (IconButton(
                        EditorGlyph::Delete,
                        "RemoveRecent",
                        EditorButtonTone::Quiet,
                        ImVec2(30.0f, 30.0f),
                        "Remove from recent projects")) {
                    std::string removeMessage{};
                    if (!recentProjects_.Remove(
                            entry.root,
                            &removeMessage)) {
                        interactionMessage_ =
                            std::move(removeMessage);
                    }
                }
                if (busy) {
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Dummy(
                ImVec2(0.0f, metrics.spacingSm));
        }
        ImGui::EndGroup();
    }

    void ProjectLauncherApp::
        DrawCreateProjectDialog() {
        if (createDialogRequested_) {
            ImGui::OpenPopup("Create HIKARI Project");
            createDialogRequested_ = false;
        }

        const ImGuiViewport* viewport =
            ImGui::GetMainViewport();
        if (viewport != nullptr) {
            ImGui::SetNextWindowPos(
                viewport->GetCenter(),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f));
        }
        ImGui::SetNextWindowSize(
            ImVec2(560.0f, 0.0f),
            ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(
                "Create HIKARI Project",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        PanelTitle(
            "New GameProject",
            "The template contains source, assets, and "
            "project settings only.");
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingMd));

        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##NewProjectName",
            "MyGame",
            projectName_.data(),
            projectName_.size());
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingSm));

        ImGui::TextUnformatted("Location");
        ImGui::SetNextItemWidth(-96.0f);
        ImGui::InputText(
            "##NewProjectLocation",
            projectLocation_.data(),
            projectLocation_.size());
        ImGui::SameLine();
        if (IconTextButton(
                EditorGlyph::Folder,
                "Browse",
                "BrowseProjectLocation",
                EditorButtonTone::Neutral,
                ImVec2(88.0f, 0.0f))) {
            BrowseForCreationLocation();
        }

        if (!interactionMessage_.empty()) {
            ImGui::Dummy(
                ImVec2(0.0f, metrics.spacingSm));
            StatusText(
                interactionMessage_.c_str(),
                EditorStatusTone::Error);
        }

        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingMd));
        ImGui::Separator();
        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingSm));
        const bool busy = openWorkflow_.IsBusy();
        if (busy) {
            ImGui::BeginDisabled();
        }
        if (ActionButton(
                "Create and Open",
                "ConfirmCreateProject",
                EditorButtonTone::Primary,
                ImVec2(140.0f, 32.0f))) {
            CreateAndOpenProject();
        }
        if (busy) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ActionButton(
                "Cancel",
                "CancelCreateProject",
                EditorButtonTone::Quiet,
                ImVec2(88.0f, 32.0f))) {
            interactionMessage_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectLauncherApp::DrawStatusPanel() {
        const EditorThemeMetrics& metrics =
            GetEditorThemeMetrics();
        const std::string& workflowMessage =
            openWorkflow_.GetStatusMessage();
        const char* message = nullptr;
        EditorStatusTone tone =
            EditorStatusTone::Normal;
        if (openWorkflow_.IsBusy()) {
            message = workflowMessage.c_str();
            tone = EditorStatusTone::Warning;
        }
        else if (openWorkflow_.GetState() ==
            ProjectOpenWorkflowState::Failed) {
            message = workflowMessage.c_str();
            tone = EditorStatusTone::Error;
        }
        else if (!interactionMessage_.empty()) {
            message = interactionMessage_.c_str();
            tone = EditorStatusTone::Error;
        }
        if (message == nullptr || message[0] == '\0') {
            return;
        }

        ImGui::Dummy(
            ImVec2(0.0f, metrics.spacingSm));
        StatusText(message, tone);
        if (openWorkflow_.IsBusy() &&
            !openWorkflow_.GetBuildLogPath().empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "Log: %s",
                TEXT::PathToUtf8(
                    openWorkflow_.
                        GetBuildLogPath()).c_str());
        }
    }

    void ProjectLauncherApp::OpenProject(
        const std::filesystem::path& projectRoot) {

        interactionMessage_.clear();
        std::string openMessage{};
        if (!openWorkflow_.Start(
                projectRoot,
                openMessage)) {
            interactionMessage_ =
                std::move(openMessage);
            return;
        }

        std::string recentMessage{};
        if (!recentProjects_.AddOrTouch(
                projectRoot,
                &recentMessage)) {
            interactionMessage_ =
                std::move(recentMessage);
        }
    }

    void ProjectLauncherApp::BrowseForProject() {
        const auto selected =
            FILE_DIALOG::PickFolder();
        if (selected.has_value()) {
            OpenProject(*selected);
        }
    }

    void ProjectLauncherApp::
        BrowseForCreationLocation() {

        const std::filesystem::path initialPath =
            TEXT::PathFromUtf8(
                projectLocation_.data());
        const auto selected =
            FILE_DIALOG::PickFolder(initialPath);
        if (selected.has_value()) {
            CopyTextToBuffer(
                TEXT::PathToUtf8(*selected),
                projectLocation_);
        }
    }

    void ProjectLauncherApp::CreateAndOpenProject() {
        ProjectCreationResult result{};
        std::string creationMessage{};
        if (!CreateProject(
                ProjectCreationRequest{
                    .parentDirectory =
                        TEXT::PathFromUtf8(
                            projectLocation_.data()),
                    .name = projectName_.data(),
                },
                result,
                creationMessage)) {
            interactionMessage_ =
                std::move(creationMessage);
            return;
        }

        projectName_.fill('\0');
        interactionMessage_.clear();
        ImGui::CloseCurrentPopup();
        OpenProject(result.projectRoot);
    }

} // namespace HIKARI::EDITOR
