#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "Editor/Projects/HIKARI_ProjectOpenWorkflow.h"
#include "Editor/Projects/HIKARI_RecentProjectStore.h"
#include "Runtime/App/HIKARI_IHostApplication.h"

namespace HIKARI::EDITOR {

    struct ProjectLauncherStartupRequest {
        std::optional<std::filesystem::path>
            rebuildProject{};
        std::optional<uint32_t> waitForProcessId{};
        bool waitForProcessIdMalformed = false;
    };

    class ProjectLauncherApp final
        : public RUNTIME::IHostApplication {
    public:
        explicit ProjectLauncherApp(
            std::filesystem::path runtimeRoot,
            ProjectLauncherStartupRequest startup = {});

        bool Initialize() override;
        void Update(float deltaTime) override;
        void Render() override;
        void RenderHostUi() override;
        bool ShouldExitHost() const override;
        void Shutdown() override;

    private:
        void DrawHeader();
        void DrawQuickStartPanel();
        void DrawRecentProjectsPanel();
        void DrawCreateProjectDialog();
        void DrawStatusPanel();

        void OpenProject(
            const std::filesystem::path& projectRoot);
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
        bool createDialogRequested_ = false;
    };

} // namespace HIKARI::EDITOR
