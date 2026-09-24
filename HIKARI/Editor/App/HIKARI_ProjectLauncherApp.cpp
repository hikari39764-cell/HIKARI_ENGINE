#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Editor/Settings/HIKARI_EditorPreferences.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include <algorithm>
#include <cstring>
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
template <size_t N>
void CopyTextToBuffer(const std::string& text, std::array<char, N>& buffer) {
    const auto count = (std::min)(text.size(), N - 1);
    std::memcpy(buffer.data(), text.data(), count);
    buffer[count] = 0;
}
} // namespace
ProjectLauncherApp::ProjectLauncherApp(std::filesystem::path runtimeRoot, ProjectLauncherStartupRequest startup)
    : runtimeRoot_(std::filesystem::absolute(std::move(runtimeRoot)).lexically_normal()), startup_(std::move(startup)),
      openWorkflow_(runtimeRoot_) {}

bool ProjectLauncherApp::Initialize() {
    std::string recentMessage{};
    if (!recentProjects_.Load(&recentMessage)) {
        interactionMessage_ = std::move(recentMessage);
    }

    const std::filesystem::path defaultParent = GetDefaultProjectParentDirectory();
    CopyTextToBuffer(GetEditorPreferences().projectParentDirectory.empty()
                         ? TEXT::PathToUtf8(defaultParent)
                         : GetEditorPreferences().projectParentDirectory,
                     projectLocation_);

    const bool rebuildWaitInvalid = startup_.rebuildProject.has_value() &&
                                    (!startup_.waitForProcessId.has_value() || *startup_.waitForProcessId == 0u);
    if (startup_.waitForProcessIdMalformed || rebuildWaitInvalid) {
        interactionMessage_ = "The rebuild handoff process ID is invalid; "
                              "the build was not started.";
    } else if (startup_.rebuildProject) {
        std::string rebuildMessage{};
        const std::filesystem::path descriptorPath = ResolveProjectDescriptorPath(*startup_.rebuildProject);
        const std::filesystem::path projectRoot = descriptorPath.parent_path();
        const bool started =
            openWorkflow_.StartRebuildAfterProcessExit(projectRoot, *startup_.waitForProcessId, rebuildMessage);
        if (!started) {
            interactionMessage_ = std::move(rebuildMessage);
        } else {
            std::string touchMessage{};
            if (!recentProjects_.AddOrTouch(projectRoot, &touchMessage)) {
                interactionMessage_ = std::move(touchMessage);
            }
        }
    }
    return true;
}

void ProjectLauncherApp::Update(float) { openWorkflow_.Update(); }
void ProjectLauncherApp::Render() {}
bool ProjectLauncherApp::ShouldExitHost() const { return openWorkflow_.ShouldExitLauncher(); }
void ProjectLauncherApp::Shutdown() {
    openWorkflow_.Shutdown();
    for (auto& [key, image] : thumbnails_)
        if (image.handle >= 0)
            DXTEX::DxTextureManager::ReleaseTextureDeferred(image.handle);
    thumbnails_.clear();
}
void ProjectLauncherApp::RenderHostUi() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 12});
    ImGui::Begin("HIKARI###ProjectHub", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    DrawHeader();
    const float statusHeight =
        openWorkflow_.GetState() == ProjectOpenWorkflowState::Idle && interactionMessage_.empty() ? 32 : 98;
    const bool narrow = viewport->WorkSize.x < 760;
    const float navigationWidth = narrow ? 54.0f : 174.0f;
    ImGui::BeginChild("navigation", {navigationWidth, -statusHeight});
    DrawNavigation();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorThemePalette().panel);
    ImGui::BeginChild("content", {0, -statusHeight}, true);
    if (page_ == Page::Projects)
        DrawRecentProjectsPanel();
    else if (page_ == Page::Create)
        DrawCreateProjectPanel();
    else if (page_ == Page::Settings)
        DrawSettings();
    else
        DrawLogs();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    DrawStatusPanel();
    ImGui::End();
}
void ProjectLauncherApp::DrawHeader() {
    ImGui::Spacing();
    PanelTitle("HIKARI", TEXT::LocalizeCStr("launcher.projectHub"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::PathToUtf8(runtimeRoot_).c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}
void ProjectLauncherApp::DrawNavigation() {
    const struct {
        Page page;
        EditorGlyph icon;
        const char* key;
    } items[]{{Page::Projects, EditorGlyph::Folder, "launcher.projects"},
              {Page::Create, EditorGlyph::NewDocument, "launcher.newProject"},
              {Page::Settings, EditorGlyph::Settings, "settings.title"},
              {Page::Logs, EditorGlyph::Log, "shell.log"}};
    const bool compact = ImGui::GetContentRegionAvail().x < 100;
    for (const auto& item : items) {
        const auto label = TEXT::Localize(item.key);
        const auto tone = page_ == item.page ? EditorButtonTone::Primary : EditorButtonTone::Quiet;
        const bool clicked = compact ? IconButton(item.icon, item.key, tone, {36, 38}, label.c_str())
                                     : IconTextButton(item.icon, label.c_str(), item.key, tone, {-1, 38});
        if (clicked) {
            page_ = item.page;
            interactionMessage_.clear();
        }
        ImGui::Spacing();
    }
}
void ProjectLauncherApp::DrawStatusPanel() {
    ImGui::Spacing();
    const auto state = openWorkflow_.GetState();
    const char* key = "launcher.idle";
    switch (state) {
    case ProjectOpenWorkflowState::Checking: key = "launcher.checking"; break;
    case ProjectOpenWorkflowState::Building: key = "launcher.building"; break;
    case ProjectOpenWorkflowState::Starting: key = "launcher.starting"; break;
    case ProjectOpenWorkflowState::WaitingForEditorExit: key = "launcher.waiting"; break;
    case ProjectOpenWorkflowState::Failed: key = "launcher.failed"; break;
    case ProjectOpenWorkflowState::Cancelled: key = "launcher.cancelled"; break;
    default: break;
    }
    const bool failed = state == ProjectOpenWorkflowState::Failed;
    StatusBadge(TEXT::LocalizeCStr(key), failed                   ? EditorStatusTone::Error
                                         : openWorkflow_.IsBusy() ? EditorStatusTone::Normal
                                                                  : EditorStatusTone::Ready);
    ImGui::SameLine();
    if (openWorkflow_.IsBusy()) {
        if (IconButton(EditorGlyph::Stop, "cancel", EditorButtonTone::Quiet, {26, 26},
                       TEXT::LocalizeCStr("ui.common.cancel")))
            openWorkflow_.Cancel();
    } else if (failed || state == ProjectOpenWorkflowState::Cancelled) {
        if (IconTextButton(EditorGlyph::Refresh, TEXT::LocalizeCStr("launcher.retry"), "retry"))
            openWorkflow_.Retry(interactionMessage_);
        if (failed) {
            NextToolbarItem();
            if (IconTextButton(EditorGlyph::File, TEXT::LocalizeCStr("launcher.authorOnly"), "authorOnly",
                               EditorButtonTone::Quiet))
                openWorkflow_.OpenWithoutModule(interactionMessage_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TEXT::LocalizeCStr("launcher.authorOnlyHint"));
        }
    }
    if (!openWorkflow_.GetBuildLogPath().empty()) {
        NextToolbarItem();
        if (IconButton(EditorGlyph::Log, "showLog", EditorButtonTone::Quiet, {26, 26}, TEXT::LocalizeCStr("shell.log")))
            page_ = Page::Logs;
    }
    if (!interactionMessage_.empty())
        ImGui::TextWrapped("%s", interactionMessage_.c_str());
    else if (failed || state == ProjectOpenWorkflowState::Cancelled || state == ProjectOpenWorkflowState::Starting)
        ImGui::TextWrapped("%s", openWorkflow_.GetStatusMessage().c_str());
}
void ProjectLauncherApp::OpenProject(const std::filesystem::path& projectRoot) {
    interactionMessage_.clear();
    std::string openMessage{};
    if (!openWorkflow_.Start(projectRoot, openMessage)) {
        interactionMessage_ = std::move(openMessage);
        return;
    }

    std::string recentMessage{};
    if (!recentProjects_.AddOrTouch(projectRoot, &recentMessage)) {
        interactionMessage_ = std::move(recentMessage);
    }
}

void ProjectLauncherApp::BrowseForProject() {
    const auto selected = FILE_DIALOG::PickFolder();
    if (selected.has_value()) {
        OpenProject(*selected);
    }
}

void ProjectLauncherApp::BrowseForCreationLocation() {
    const std::filesystem::path initialPath = TEXT::PathFromUtf8(projectLocation_.data());
    const auto selected = FILE_DIALOG::PickFolder(initialPath);
    if (selected.has_value()) {
        CopyTextToBuffer(TEXT::PathToUtf8(*selected), projectLocation_);
    }
}

void ProjectLauncherApp::CreateAndOpenProject() {
    ProjectCreationResult result{};
    std::string creationMessage{};
    if (!CreateProject(
            ProjectCreationRequest{
                .parentDirectory = TEXT::PathFromUtf8(projectLocation_.data()),
                .name = projectName_.data(),
            },
            result, creationMessage)) {
        interactionMessage_ = std::move(creationMessage);
        return;
    }

    projectName_.fill('\0');
    interactionMessage_.clear();
    auto preferences = GetEditorPreferences();
    preferences.projectParentDirectory = projectLocation_.data();
    (void)SaveEditorPreferences(preferences);
    page_ = Page::Projects;
    OpenProject(result.projectRoot);
}

} // namespace HIKARI::EDITOR
