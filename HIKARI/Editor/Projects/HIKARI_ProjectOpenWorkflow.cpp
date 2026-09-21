#include <fstream>
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Projects/HIKARI_ProjectOpenWorkflow.h"

#include <system_error>
#include <utility>
#include <vector>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::EDITOR {
namespace {

    const char* SelectEditorPreset() noexcept {
#if defined(_DEBUG)
        return "debug";
#else
        return "editor-release";
#endif
    }

} // namespace

    ProjectOpenWorkflow::ProjectOpenWorkflow(
        std::filesystem::path runtimeRoot)
        : runtimeRoot_(
            std::filesystem::absolute(
                std::move(runtimeRoot))
                .lexically_normal()) {
    }

    ProjectOpenWorkflow::~ProjectOpenWorkflow() {
        Shutdown();
    }

    bool ProjectOpenWorkflow::Start(
        const std::filesystem::path& projectRoot,
        std::string& outMessage) {

        return StartRequest(
            projectRoot,
            ProjectOpenWorkflowRequest::OpenProject,
            outMessage);
    }

    bool ProjectOpenWorkflow::
        StartRebuildAfterProcessExit(
            const std::filesystem::path& projectRoot,
            uint32_t processId,
            std::string& outMessage) {

        if (IsBusy()) {
            outMessage =
                "A project build is already running.";
            return false;
        }
        if (processId == 0u ||
            processId == GetCurrentProcessId()) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                "The previous project editor process ID is invalid; "
                "the rebuild was not started.";
            outMessage = statusMessage_;
            return false;
        }

        shouldExitLauncher_ = false;
        buildLogPath_.clear();
        projectRoot_ =
            PROJECT_PATHS::NormalizeProjectRoot(projectRoot);
        request_ =
            ProjectOpenWorkflowRequest::RebuildAndReload;
        waitProcess_ = OpenProcess(
            SYNCHRONIZE,
            FALSE,
            static_cast<DWORD>(processId));
        if (waitProcess_ == nullptr) {
            const DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER) {
                return BeginBuild(
                    projectRoot_,
                    request_,
                    outMessage);
            }
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not wait for the previous project editor",
                    error);
            outMessage = statusMessage_;
            return false;
        }

        state_ =
            ProjectOpenWorkflowState::WaitingForEditorExit;
        statusMessage_ =
            "Waiting for the previous project editor to close "
            "before rebuilding its GameProject module...";
        outMessage.clear();
        return true;
    }

    bool ProjectOpenWorkflow::StartRequest(
        const std::filesystem::path& projectRoot,
        ProjectOpenWorkflowRequest request,
        std::string& outMessage) {

        if (IsBusy()) {
            outMessage =
                "A project build is already running.";
            return false;
        }
        return BeginBuild(projectRoot, request, outMessage);
    }

    bool ProjectOpenWorkflow::BeginBuild(
        const std::filesystem::path& projectRoot,
        ProjectOpenWorkflowRequest request,
        std::string& outMessage) {

        shouldExitLauncher_ = false;
        projectRoot_ =
            PROJECT_PATHS::NormalizeProjectRoot(projectRoot);
        request_ = request;
        descriptorPath_ =
            projectRoot_ / kProjectDescriptorFilename;

        ProjectDescriptor descriptor{};
        if (projectRoot_.empty() ||
            !LoadProjectDescriptor(
                descriptorPath_,
                descriptor,
                &outMessage)) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ = outMessage.empty()
                ? "The selected folder is not a HIKARI project."
                : outMessage;
            outMessage = statusMessage_;
            return false;
        }

        const std::filesystem::path manifestPath =
            (projectRoot_ /
                descriptor.gameProjectManifest)
                .lexically_normal();
        std::error_code ec{};
        if (!std::filesystem::is_regular_file(
                manifestPath,
                ec)) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                "The GameProject CMake manifest is missing: " +
                TEXT::PathToUtf8(manifestPath);
            outMessage = statusMessage_;
            return false;
        }

        const std::filesystem::path buildScript =
            runtimeRoot_ / "Sdk" / "Tools" /
            "Build-HikariProject.ps1";
        if (!std::filesystem::is_regular_file(
                buildScript,
                ec)) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                "The HIKARI build entry point is missing: " +
                TEXT::PathToUtf8(buildScript);
            outMessage = statusMessage_;
            return false;
        }

        buildLogPath_ =
            projectRoot_ / "Library" / "Build" /
            "HIKARI_ProjectBuild.log";
        std::filesystem::create_directories(
            buildLogPath_.parent_path(),
            ec);
        if (ec) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                "Could not create the project build log directory: " +
                ec.message();
            outMessage = statusMessage_;
            return false;
        }

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength =
            sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;
        HANDLE logHandle = CreateFileW(
            buildLogPath_.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &securityAttributes,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (logHandle == INVALID_HANDLE_VALUE) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not create the project build log",
                    GetLastError());
            outMessage = statusMessage_;
            return false;
        }

        preset_ = SelectEditorPreset();
        std::wstring commandLine{};
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"powershell.exe");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-NoProfile");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-ExecutionPolicy");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"Bypass");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-File");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine,
            buildScript.wstring());
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-Preset");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine,
            TEXT::PathFromUtf8(preset_).wstring());
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-Project");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine,
            projectRoot_.wstring());
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine, L"-SdkRoot");
        PROJECT_PROCESS::AppendWindowsArgument(
            commandLine,
            (runtimeRoot_ / "Sdk").wstring());
        if (request_ ==
            ProjectOpenWorkflowRequest::RebuildAndReload) {
            PROJECT_PROCESS::AppendWindowsArgument(
                commandLine,
                L"-ForceBuild");
        }

        std::vector<wchar_t> mutableCommandLine(
            commandLine.begin(),
            commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = logHandle;
        startupInfo.hStdError = logHandle;
        startupInfo.hStdInput =
            GetStdHandle(STD_INPUT_HANDLE);

        HANDLE buildJob = CreateJobObjectW(
            nullptr,
            nullptr);
        if (buildJob == nullptr) {
            const DWORD error = GetLastError();
            CloseHandle(logHandle);
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not create the HIKARI project build job",
                    error);
            outMessage = statusMessage_;
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
        jobLimits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                buildJob,
                JobObjectExtendedLimitInformation,
                &jobLimits,
                sizeof(jobLimits))) {
            const DWORD error = GetLastError();
            CloseHandle(buildJob);
            CloseHandle(logHandle);
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not configure the HIKARI project build job",
                    error);
            outMessage = statusMessage_;
            return false;
        }

        PROCESS_INFORMATION processInformation{};
        const BOOL created = CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED |
                CREATE_NO_WINDOW |
                CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            runtimeRoot_.c_str(),
            &startupInfo,
            &processInformation);
        const DWORD createError = created
            ? ERROR_SUCCESS
            : GetLastError();
        CloseHandle(logHandle);
        if (!created) {
            CloseHandle(buildJob);
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not start the HIKARI project build",
                    createError);
            outMessage = statusMessage_;
            return false;
        }

        if (!AssignProcessToJobObject(
                buildJob,
                processInformation.hProcess)) {
            const DWORD error = GetLastError();
            TerminateProcess(
                processInformation.hProcess,
                error);
            WaitForSingleObject(
                processInformation.hProcess,
                5000u);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            CloseHandle(buildJob);
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not contain the HIKARI project build process",
                    error);
            outMessage = statusMessage_;
            return false;
        }
        if (ResumeThread(processInformation.hThread) ==
            static_cast<DWORD>(-1)) {
            const DWORD error = GetLastError();
            TerminateJobObject(buildJob, error);
            CloseHandle(buildJob);
            WaitForSingleObject(
                processInformation.hProcess,
                5000u);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not resume the HIKARI project build process",
                    error);
            outMessage = statusMessage_;
            return false;
        }

        CloseHandle(processInformation.hThread);
        buildProcess_ = processInformation.hProcess;
        buildJob_ = buildJob;
        state_ = ProjectOpenWorkflowState::Checking;
        logTail_.clear(); logPoll_ = {};
        statusMessage_ = request_ ==
                ProjectOpenWorkflowRequest::RebuildAndReload
            ? "Rebuilding " + descriptor.name +
                " with the shared HIKARI " +
                preset_ + " SDK before reloading..."
            : "Building " + descriptor.name +
                " with the shared HIKARI " +
                preset_ + " build...";
        outMessage.clear();
        return true;
    }

    void ProjectOpenWorkflow::Update() {
        if (state_ == ProjectOpenWorkflowState::Starting) {
            if (WaitForSingleObject(editorProcess_, 0) == WAIT_OBJECT_0) {
                DWORD code = 0; GetExitCodeProcess(editorProcess_, &code);
                state_ = ProjectOpenWorkflowState::Failed;
                statusMessage_ = TEXT::Localize("launcher.earlyExit") + " (" + std::to_string(code) + ")";
                CloseEditorWait();
            } else if (WaitForSingleObject(readyEvent_, 0) == WAIT_OBJECT_0) {
                shouldExitLauncher_ = true;
                state_ = ProjectOpenWorkflowState::Idle;
                statusMessage_ = TEXT::Localize("launcher.ready");
                CloseEditorWait();
            } else if (std::chrono::steady_clock::now() - launchStarted_ > std::chrono::seconds(120)) {
                statusMessage_ = TEXT::Localize("launcher.slowStart");
            }
            return;
        }
        if (state_ == ProjectOpenWorkflowState::Checking || state_ == ProjectOpenWorkflowState::Building) PollBuildLog();
        if (state_ ==
            ProjectOpenWorkflowState::WaitingForEditorExit) {
            if (waitProcess_ == nullptr) {
                state_ = ProjectOpenWorkflowState::Failed;
                statusMessage_ =
                    "The previous editor wait handle is unavailable.";
                return;
            }
            const DWORD waitResult =
                WaitForSingleObject(waitProcess_, 0u);
            if (waitResult == WAIT_TIMEOUT) {
                return;
            }
            if (waitResult != WAIT_OBJECT_0) {
                statusMessage_ =
                    PROJECT_PROCESS::MakeWindowsError(
                        "Could not observe the previous project editor",
                        GetLastError());
                state_ = ProjectOpenWorkflowState::Failed;
                CloseWaitProcess();
                return;
            }

            CloseWaitProcess();
            const std::filesystem::path projectRoot =
                projectRoot_;
            state_ = ProjectOpenWorkflowState::Idle;
            std::string startMessage{};
            if (!BeginBuild(
                    projectRoot,
                    ProjectOpenWorkflowRequest::RebuildAndReload,
                    startMessage)) {
                statusMessage_ = std::move(startMessage);
            }
            return;
        }

        if ((state_ != ProjectOpenWorkflowState::Building && state_ != ProjectOpenWorkflowState::Checking) ||
            buildProcess_ == nullptr) {
            return;
        }

        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(
                buildProcess_,
                &exitCode)) {
            statusMessage_ =
                PROJECT_PROCESS::MakeWindowsError(
                    "Could not query the HIKARI project build",
                    GetLastError());
            state_ = ProjectOpenWorkflowState::Failed;
            CancelBuildProcess();
            return;
        }
        if (exitCode == STILL_ACTIVE) {
            return;
        }

        CloseBuildProcess();
        logPoll_ = {}; // 終了直前の診断をポーリング間隔で取りこぼさない。
        PollBuildLog();
        if (exitCode != 0u) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ = TEXT::Localize("launcher.buildFailed") + " (" + std::to_string(exitCode) + ")";
            return;
        }
        std::string message;
        if (!LaunchEditor(true, message)) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ = std::move(message);
        }
    }

    void ProjectOpenWorkflow::Shutdown() noexcept {
        CancelBuildProcess();
        CloseWaitProcess();
        CloseEditorWait();
    }

    ProjectOpenWorkflowState
        ProjectOpenWorkflow::GetState() const noexcept {
        return state_;
    }

    bool ProjectOpenWorkflow::IsBusy() const noexcept {
        return state_ ==
                ProjectOpenWorkflowState::WaitingForEditorExit ||
            state_ == ProjectOpenWorkflowState::Building ||
            state_ == ProjectOpenWorkflowState::Checking ||
            state_ == ProjectOpenWorkflowState::Starting;
    }

    bool ProjectOpenWorkflow::
        ShouldExitLauncher() const noexcept {
        return shouldExitLauncher_;
    }

    const std::string&
        ProjectOpenWorkflow::
            GetStatusMessage() const noexcept {
        return statusMessage_;
    }

    const std::filesystem::path&
        ProjectOpenWorkflow::
            GetBuildLogPath() const noexcept {
        return buildLogPath_;
    }

    bool ProjectOpenWorkflow::LaunchEditor(
        bool useProjectModule,
        std::string& outMessage) {

        const std::filesystem::path executable =
            runtimeRoot_ / "HIKARI_Engine.exe";
        const std::filesystem::path projectModule =
            projectRoot_ / "Library" / "Build" /
            preset_ / "bin" /
            "HIKARI_GameProject.dll";
        const std::filesystem::path projectEditorModule =
            projectRoot_ / "Library" / "Build" /
            preset_ / "bin" /
            "HIKARI_GameProjectEditor.dll";
        std::error_code ec{};
        if (!std::filesystem::is_regular_file(
                executable,
                ec)) {
            outMessage =
                "The project editor executable was not produced: " +
                TEXT::PathToUtf8(executable);
            return false;
        }
        if (useProjectModule &&
            !std::filesystem::is_regular_file(
                projectModule,
                ec)) {
            outMessage =
                "The GameProject module was not produced: " +
                TEXT::PathToUtf8(projectModule);
            return false;
        }

        std::vector<std::wstring> arguments{
            L"--project",
            descriptorPath_.wstring(),
        };
        if (useProjectModule) {
            arguments.push_back(L"--project-module");
            arguments.push_back(projectModule.wstring());
            ec.clear();
            if (std::filesystem::is_regular_file(
                    projectEditorModule,
                    ec) && !ec) {
                arguments.push_back(L"--project-editor-module");
                arguments.push_back(
                    projectEditorModule.wstring());
            }
        }
        else {
            arguments.push_back(
                L"--allow-missing-project-module");
        }
        CloseEditorWait();
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid))) { outMessage = "Could not allocate startup handshake."; return false; }
        wchar_t token[40]{}; StringFromGUID2(guid, token, 40);
        readyEventName_ = std::wstring(L"Local\\HIKARI.EditorReady.") + token;
        readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, readyEventName_.c_str());
        if (!readyEvent_) { outMessage = "Could not create startup handshake."; return false; }
        arguments.push_back(L"--launcher-ready-event"); arguments.push_back(readyEventName_);
        const bool launched = PROJECT_PROCESS::LaunchProcess(
            executable,
            arguments,
            projectRoot_,
            "Could not start the project editor",
            outMessage, &editorProcess_);
        if (!launched) { CloseEditorWait(); return false; }
        launchStarted_ = std::chrono::steady_clock::now();
        state_ = ProjectOpenWorkflowState::Starting;
        statusMessage_ = TEXT::Localize("launcher.starting");
        return true;
    }

    void ProjectOpenWorkflow::
        CloseBuildProcess() noexcept {

        if (buildProcess_ != nullptr) {
            CloseHandle(buildProcess_);
            buildProcess_ = nullptr;
        }
        if (buildJob_ != nullptr) {
            CloseHandle(buildJob_);
            buildJob_ = nullptr;
        }
    }

    void ProjectOpenWorkflow::
        CancelBuildProcess() noexcept {

        if (buildJob_ != nullptr) {
            TerminateJobObject(buildJob_, 1u);
            CloseHandle(buildJob_);
            buildJob_ = nullptr;
        }
        else if (buildProcess_ != nullptr) {
            DWORD exitCode = STILL_ACTIVE;
            if (GetExitCodeProcess(
                    buildProcess_,
                    &exitCode) &&
                exitCode == STILL_ACTIVE) {
                TerminateProcess(buildProcess_, 1u);
            }
        }
        if (buildProcess_ != nullptr) {
            WaitForSingleObject(buildProcess_, 5000u);
            CloseHandle(buildProcess_);
            buildProcess_ = nullptr;
        }
    }

    void ProjectOpenWorkflow::
        CloseWaitProcess() noexcept {

        if (waitProcess_ != nullptr) {
            CloseHandle(waitProcess_);
            waitProcess_ = nullptr;
        }
    }

    void ProjectOpenWorkflow::CloseEditorWait() noexcept {
        if (editorProcess_) { CloseHandle(editorProcess_); editorProcess_ = nullptr; }
        if (readyEvent_) { CloseHandle(readyEvent_); readyEvent_ = nullptr; }
        readyEventName_.clear();
    }

    void ProjectOpenWorkflow::Cancel() noexcept {
        const bool started = state_ == ProjectOpenWorkflowState::Starting;
        CancelBuildProcess(); CloseWaitProcess(); CloseEditorWait();
        shouldExitLauncher_ = false;
        state_ = ProjectOpenWorkflowState::Cancelled;
        statusMessage_ = TEXT::Localize(started ? "launcher.detached" : "launcher.cancelled");
    }

    bool ProjectOpenWorkflow::Retry(std::string& message) {
        if (IsBusy() || projectRoot_.empty()) return false;
        return StartRequest(projectRoot_, request_, message);
    }

    bool ProjectOpenWorkflow::OpenWithoutModule(std::string& message) {
        if (IsBusy() || descriptorPath_.empty()) return false;
        return LaunchEditor(false, message);
    }

    void ProjectOpenWorkflow::PollBuildLog() {
        const auto now = std::chrono::steady_clock::now();
        if (now - logPoll_ < std::chrono::milliseconds(250)) return;
        logPoll_ = now;
        std::ifstream input(buildLogPath_, std::ios::binary | std::ios::ate);
        if (!input) return;
        const auto size = input.tellg();
        if (size < 0) return;
        const auto begin = (std::max)(std::streamoff{0}, static_cast<std::streamoff>(size) - 32768);
        input.seekg(begin); logTail_.assign(std::istreambuf_iterator<char>(input), {});
        if (begin > 0) { const auto line = logTail_.find('\n'); if (line != std::string::npos) logTail_.erase(0, line + 1); }
        if (logTail_.find("HIKARI_STAGE building") != std::string::npos)
            state_ = ProjectOpenWorkflowState::Building;
    }

} // namespace HIKARI::EDITOR
