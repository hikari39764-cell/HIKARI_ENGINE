#pragma once

#include <filesystem>
#include <string>

#include <Windows.h>

namespace HIKARI::EDITOR {

    enum class ProjectOpenWorkflowState {
        Idle,
        Building,
        Failed,
    };

    class ProjectOpenWorkflow {
    public:
        explicit ProjectOpenWorkflow(
            std::filesystem::path engineRoot);
        ~ProjectOpenWorkflow();

        ProjectOpenWorkflow(
            const ProjectOpenWorkflow&) = delete;
        ProjectOpenWorkflow& operator=(
            const ProjectOpenWorkflow&) = delete;

        bool Start(
            const std::filesystem::path& projectRoot,
            std::string& outMessage);
        void Update();
        void Shutdown() noexcept;

        ProjectOpenWorkflowState GetState() const noexcept;
        bool IsBusy() const noexcept;
        bool ShouldExitLauncher() const noexcept;
        const std::string& GetStatusMessage() const noexcept;
        const std::filesystem::path& GetBuildLogPath() const noexcept;

    private:
        bool LaunchEditor(std::string& outMessage);
        void CloseBuildProcess() noexcept;

        std::filesystem::path engineRoot_{};
        std::filesystem::path projectRoot_{};
        std::filesystem::path descriptorPath_{};
        std::filesystem::path buildLogPath_{};
        std::string preset_{};
        std::string statusMessage_{};
        HANDLE buildProcess_ = nullptr;
        ProjectOpenWorkflowState state_ =
            ProjectOpenWorkflowState::Idle;
        bool shouldExitLauncher_ = false;
    };

} // namespace HIKARI::EDITOR
