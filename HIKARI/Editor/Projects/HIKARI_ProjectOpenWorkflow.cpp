#include "Editor/Projects/HIKARI_ProjectOpenWorkflow.h"

#include <system_error>
#include <utility>
#include <vector>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::EDITOR {
namespace {

    std::wstring QuoteWindowsArgument(
        const std::wstring& argument) {

        if (argument.empty()) {
            return L"\"\"";
        }
        if (argument.find_first_of(L" \t\n\v\"") ==
            std::wstring::npos) {
            return argument;
        }

        std::wstring quoted{ L'"' };
        size_t backslashCount = 0u;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashCount;
                continue;
            }
            if (character == L'"') {
                quoted.append(
                    backslashCount * 2u + 1u,
                    L'\\');
                quoted.push_back(L'"');
                backslashCount = 0u;
                continue;
            }
            quoted.append(backslashCount, L'\\');
            backslashCount = 0u;
            quoted.push_back(character);
        }
        quoted.append(backslashCount * 2u, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    void AppendArgument(
        std::wstring& commandLine,
        const std::wstring& argument) {

        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteWindowsArgument(argument);
    }

    std::string MakeWindowsError(
        const char* action,
        DWORD error) {

        return std::string(action) +
            " (Windows error " +
            std::to_string(error) +
            ").";
    }

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

        if (IsBusy()) {
            outMessage =
                "A project build is already running.";
            return false;
        }

        shouldExitLauncher_ = false;
        projectRoot_ =
            PROJECT_PATHS::NormalizeProjectRoot(projectRoot);
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
            statusMessage_ = MakeWindowsError(
                "Could not create the project build log",
                GetLastError());
            outMessage = statusMessage_;
            return false;
        }

        preset_ = SelectEditorPreset();
        std::wstring commandLine{};
        AppendArgument(commandLine, L"powershell.exe");
        AppendArgument(commandLine, L"-NoProfile");
        AppendArgument(commandLine, L"-ExecutionPolicy");
        AppendArgument(commandLine, L"Bypass");
        AppendArgument(commandLine, L"-File");
        AppendArgument(
            commandLine,
            buildScript.wstring());
        AppendArgument(commandLine, L"-Preset");
        AppendArgument(
            commandLine,
            TEXT::PathFromUtf8(preset_).wstring());
        AppendArgument(commandLine, L"-Project");
        AppendArgument(
            commandLine,
            projectRoot_.wstring());
        AppendArgument(commandLine, L"-SdkRoot");
        AppendArgument(
            commandLine,
            (runtimeRoot_ / "Sdk").wstring());

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

        PROCESS_INFORMATION processInformation{};
        const BOOL created = CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
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
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ = MakeWindowsError(
                "Could not start the HIKARI project build",
                createError);
            outMessage = statusMessage_;
            return false;
        }

        CloseHandle(processInformation.hThread);
        buildProcess_ = processInformation.hProcess;
        state_ = ProjectOpenWorkflowState::Building;
        statusMessage_ =
            "Building " + descriptor.name +
            " with the shared HIKARI " +
            preset_ + " build...";
        outMessage.clear();
        return true;
    }

    void ProjectOpenWorkflow::Update() {
        if (!IsBusy() || buildProcess_ == nullptr) {
            return;
        }

        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(
                buildProcess_,
                &exitCode)) {
            statusMessage_ = MakeWindowsError(
                "Could not query the HIKARI project build",
                GetLastError());
            state_ = ProjectOpenWorkflowState::Failed;
            CloseBuildProcess();
            return;
        }
        if (exitCode == STILL_ACTIVE) {
            return;
        }

        CloseBuildProcess();
        if (exitCode != 0u) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ =
                "Project build failed with exit code " +
                std::to_string(exitCode) +
                ". See " +
                TEXT::PathToUtf8(buildLogPath_);
            return;
        }

        std::string launchMessage{};
        if (!LaunchEditor(launchMessage)) {
            state_ = ProjectOpenWorkflowState::Failed;
            statusMessage_ = std::move(launchMessage);
            return;
        }
        state_ = ProjectOpenWorkflowState::Idle;
        statusMessage_ =
            "Project editor started.";
        shouldExitLauncher_ = true;
    }

    void ProjectOpenWorkflow::Shutdown() noexcept {
        CloseBuildProcess();
    }

    ProjectOpenWorkflowState
        ProjectOpenWorkflow::GetState() const noexcept {
        return state_;
    }

    bool ProjectOpenWorkflow::IsBusy() const noexcept {
        return state_ ==
            ProjectOpenWorkflowState::Building;
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
        std::string& outMessage) {

        const std::filesystem::path executable =
            runtimeRoot_ / "HIKARI_UpdateTo3D.exe";
        const std::filesystem::path projectModule =
            projectRoot_ / "Library" / "Build" /
            preset_ / "bin" /
            "HIKARI_GameProject.dll";
        std::error_code ec{};
        if (!std::filesystem::is_regular_file(
                executable,
                ec)) {
            outMessage =
                "The project editor executable was not produced: " +
                TEXT::PathToUtf8(executable);
            return false;
        }
        if (!std::filesystem::is_regular_file(
                projectModule,
                ec)) {
            outMessage =
                "The GameProject module was not produced: " +
                TEXT::PathToUtf8(projectModule);
            return false;
        }

        std::wstring commandLine{};
        AppendArgument(
            commandLine,
            executable.wstring());
        AppendArgument(commandLine, L"--project");
        AppendArgument(
            commandLine,
            descriptorPath_.wstring());
        AppendArgument(
            commandLine,
            L"--project-module");
        AppendArgument(
            commandLine,
            projectModule.wstring());
        std::vector<wchar_t> mutableCommandLine(
            commandLine.begin(),
            commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation{};
        if (!CreateProcessW(
                executable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NEW_PROCESS_GROUP |
                    CREATE_UNICODE_ENVIRONMENT,
                nullptr,
                projectRoot_.c_str(),
                &startupInfo,
                &processInformation)) {
            outMessage = MakeWindowsError(
                "Could not start the project editor",
                GetLastError());
            return false;
        }

        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);
        outMessage.clear();
        return true;
    }

    void ProjectOpenWorkflow::
        CloseBuildProcess() noexcept {

        if (buildProcess_ != nullptr) {
            CloseHandle(buildProcess_);
            buildProcess_ = nullptr;
        }
    }

} // namespace HIKARI::EDITOR
