#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <string>

#include <Windows.h>

namespace HIKARI::EDITOR {

    enum class ProjectOpenWorkflowState {
        Idle,
        WaitingForEditorExit,
        Checking,
        Building,
        Starting,
        Cancelled,
        Failed,
    };

    enum class ProjectOpenWorkflowRequest {
        OpenProject,
        RebuildAndReload,
    };

    class ProjectOpenWorkflow {
    public:
        explicit ProjectOpenWorkflow(
            std::filesystem::path runtimeRoot);
        ~ProjectOpenWorkflow();

        ProjectOpenWorkflow(
            const ProjectOpenWorkflow&) = delete;
        ProjectOpenWorkflow& operator=(
            const ProjectOpenWorkflow&) = delete;

        bool Start(
            const std::filesystem::path& projectRoot,
            std::string& outMessage);
        bool StartRebuildAfterProcessExit(
            const std::filesystem::path& projectRoot,
            uint32_t processId,
            std::string& outMessage);
        void Update();
        void Cancel() noexcept;
        bool Retry(std::string& message);
        bool OpenWithoutModule(std::string& message);
        const std::string& GetLogTail() const noexcept { return logTail_; }
        void Shutdown() noexcept;

        ProjectOpenWorkflowState GetState() const noexcept;
        bool IsBusy() const noexcept;
        bool ShouldExitLauncher() const noexcept;
        const std::string& GetStatusMessage() const noexcept;
        const std::filesystem::path& GetBuildLogPath() const noexcept;

    private:
        bool StartRequest(
            const std::filesystem::path& projectRoot,
            ProjectOpenWorkflowRequest request,
            std::string& outMessage);
        bool BeginBuild(
            const std::filesystem::path& projectRoot,
            ProjectOpenWorkflowRequest request,
            std::string& outMessage);
        bool LaunchEditor(
            bool useProjectModule,
            std::string& outMessage);
        void CloseBuildProcess() noexcept;
        void CancelBuildProcess() noexcept;
        void CloseWaitProcess() noexcept;
        void CloseEditorWait() noexcept;
        void PollBuildLog();

        std::filesystem::path runtimeRoot_{};
        std::filesystem::path projectRoot_{};
        std::filesystem::path descriptorPath_{};
        std::filesystem::path buildLogPath_{};
        std::string preset_{};
        std::string statusMessage_{};
        HANDLE buildProcess_ = nullptr;
        HANDLE buildJob_ = nullptr;
        HANDLE waitProcess_ = nullptr;
        HANDLE editorProcess_ = nullptr;
        HANDLE readyEvent_ = nullptr;
        std::wstring readyEventName_;
        std::string logTail_;
        std::chrono::steady_clock::time_point logPoll_{}, launchStarted_{};
        ProjectOpenWorkflowState state_ =
            ProjectOpenWorkflowState::Idle;
        ProjectOpenWorkflowRequest request_ =
            ProjectOpenWorkflowRequest::OpenProject;
        bool shouldExitLauncher_ = false;
    };

} // namespace HIKARI::EDITOR
