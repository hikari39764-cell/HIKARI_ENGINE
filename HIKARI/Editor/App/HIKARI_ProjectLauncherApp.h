#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <map>

#include "Editor/Projects/HIKARI_ProjectOpenWorkflow.h"
#include "Editor/Diagnostics/HIKARI_EditorLogPanel.h"
#include "Editor/Projects/HIKARI_RecentProjectStore.h"
#include "Runtime/App/HIKARI_IHostApplication.h"

namespace HIKARI::EDITOR {

struct ProjectLauncherStartupRequest {
    std::optional<std::filesystem::path> rebuildProject{};
    std::optional<uint32_t> waitForProcessId{};
    bool waitForProcessIdMalformed = false;
};

class ProjectLauncherApp final : public RUNTIME::IHostApplication {
public:
    explicit ProjectLauncherApp(std::filesystem::path runtimeRoot, ProjectLauncherStartupRequest startup = {});

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Render() override;
    void RenderHostUi() override;
    bool ShouldExitHost() const override;
    void Shutdown() override;

private:
    void DrawHeader();
    void DrawNavigation();
    void DrawSettings();
    void DrawLogs();
    void DrawRecentProjectsPanel();
    void DrawCreateProjectPanel();
    void DrawStatusPanel();

    void OpenProject(const std::filesystem::path& projectRoot);
    void BrowseForProject();
    void BrowseForCreationLocation();
    void CreateAndOpenProject();

    std::filesystem::path runtimeRoot_{};
    ProjectLauncherStartupRequest startup_{};
    RecentProjectStore recentProjects_{};
    ProjectOpenWorkflow openWorkflow_;
    std::array<char, 128u> projectName_{};
    std::array<char, 1024u> projectLocation_{};
    std::string interactionMessage_{};
    enum class Page { Projects, Create, Settings, Logs };
    Page page_ = Page::Projects;
    EditorLogPanel logPanel_;
    std::array<char, 256> search_{};
    bool pinnedOnly_ = false;
    struct Thumbnail {
        int handle = -1;
        std::string path;
    };
    std::map<std::string, Thumbnail> thumbnails_;
};

} // namespace HIKARI::EDITOR
