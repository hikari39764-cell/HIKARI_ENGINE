#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include <Windows.h>

namespace HIKARI::EDITOR {
    struct EditorChildProcess::State {
        HANDLE process = nullptr;
        HANDLE job = nullptr;
        ~State() { if (process) CloseHandle(process); if (job) CloseHandle(job); }
    };
    EditorChildProcess::EditorChildProcess() : state_(std::make_unique<State>()) {}
    EditorChildProcess::~EditorChildProcess() = default;
    bool EditorChildProcess::Running() const noexcept { return state_->process && WaitForSingleObject(state_->process, 0) == WAIT_TIMEOUT; }
    uint32_t EditorChildProcess::ExitCode() const noexcept {
        DWORD code = 1;
        if (state_->process) GetExitCodeProcess(state_->process, &code);
        return code;
    }
    void EditorChildProcess::Wait(uint32_t milliseconds) const noexcept {
        if (state_->process) WaitForSingleObject(state_->process, milliseconds);
    }
    void EditorChildProcess::Cancel() noexcept { if (state_->job && Running()) TerminateJobObject(state_->job, ERROR_CANCELLED); }
    std::filesystem::path EditorChildProcess::PowerShell() {
        wchar_t directory[MAX_PATH]{};
        GetSystemDirectoryW(directory, MAX_PATH);
        return std::filesystem::path(directory) / "WindowsPowerShell/v1.0/powershell.exe";
    }
    bool EditorChildProcess::Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
        const std::filesystem::path& directory, const std::filesystem::path& log, std::string& message, bool killOnClose) {
        if (Running()) { message = "This worker is still running."; return false; }
        state_ = std::make_unique<State>();
        std::wstring command;
        PROJECT_PROCESS::AppendWindowsArgument(command, executable.wstring());
        for (const auto& argument : arguments) PROJECT_PROCESS::AppendWindowsArgument(command, argument);
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        struct CloseStreams {
            HANDLE output, input;
            ~CloseStreams() { if (output != INVALID_HANDLE_VALUE) CloseHandle(output); if (input != INVALID_HANDLE_VALUE) CloseHandle(input); }
        } closeStreams{output, input};
        if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) { message = "Cannot open worker streams."; return false; }
        state_->job = CreateJobObjectW(nullptr, nullptr);
        if (!state_->job) { message = "Cannot own the worker process tree."; return false; }
        if (killOnClose) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(state_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                message = "Cannot configure worker lifetime."; return false;
            }
        }
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<std::byte> attributes(attributeBytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) { message = "Cannot initialize worker handles."; return false; }
        struct DeleteAttributes { LPPROC_THREAD_ATTRIBUTE_LIST list; ~DeleteAttributes() { DeleteProcThreadAttributeList(list); } } deleteAttributes{list};
        HANDLE handles[]{output, input};
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) {
            message = "Cannot isolate inherited worker handles."; return false;
        }
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = output;
        startup.StartupInfo.hStdError = output;
        startup.StartupInfo.hStdInput = input;
        startup.lpAttributeList = list;
        std::vector<wchar_t> environment;
        if (auto* inherited = GetEnvironmentStringsW()) {
            for (const wchar_t* entry = inherited; *entry; entry += wcslen(entry) + 1u) {
                if (_wcsnicmp(entry, L"PSModulePath=", 13) != 0)
                    environment.insert(environment.end(), entry, entry + wcslen(entry) + 1u);
            }
            FreeEnvironmentStringsW(inherited);
        }
        environment.push_back(L'\0');
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            environment.size() > 1 ? environment.data() : nullptr, directory.c_str(), &startup.StartupInfo, &info)) {
            message = PROJECT_PROCESS::MakeWindowsError("Cannot start worker", GetLastError()); return false;
        }
        state_->process = info.hProcess;
        if (!AssignProcessToJobObject(state_->job, state_->process)) {
            TerminateProcess(state_->process, 1);
            CloseHandle(info.hThread);
            message = "Cannot assign the new worker to its owned job.";
            return false;
        }
        const auto resumed = ResumeThread(info.hThread);
        CloseHandle(info.hThread);
        if (resumed == static_cast<DWORD>(-1)) { Cancel(); message = "Cannot resume worker."; return false; }
        return true;
    }
}
