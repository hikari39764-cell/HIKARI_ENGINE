#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include <Windows.h>

#include <algorithm>
#include <utility>

namespace HIKARI::EDITOR {
struct EditorChildProcess::State {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    // writableInput の時の、標準入力の管道の書き側。
    HANDLE input = nullptr;
    ~State() {
        if (input)
            CloseHandle(input);
        if (process)
            CloseHandle(process);
        if (job)
            CloseHandle(job);
    }
};
EditorChildProcess::EditorChildProcess() : state_(std::make_unique<State>()) {}
EditorChildProcess::~EditorChildProcess() = default;
bool EditorChildProcess::Running() const noexcept {
    return state_->process && WaitForSingleObject(state_->process, 0) == WAIT_TIMEOUT;
}
uint32_t EditorChildProcess::ExitCode() const noexcept {
    DWORD code = 1;
    if (state_->process)
        GetExitCodeProcess(state_->process, &code);
    return code;
}
void EditorChildProcess::Wait(uint32_t milliseconds) const noexcept {
    if (state_->process)
        WaitForSingleObject(state_->process, milliseconds);
}
bool EditorChildProcess::WriteInput(std::string_view bytes) noexcept {
    while (state_->input && !bytes.empty()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>((std::min)(bytes.size(), size_t{1u} << 20u));
        if (!WriteFile(state_->input, bytes.data(), chunk, &written, nullptr) || written == 0)
            return false;
        bytes.remove_prefix(written);
    }
    return state_->input != nullptr;
}
void EditorChildProcess::CloseInput() noexcept {
    if (state_->input) {
        CloseHandle(state_->input);
        state_->input = nullptr;
    }
}
void EditorChildProcess::Cancel() noexcept {
    if (state_->job && Running())
        TerminateJobObject(state_->job, ERROR_CANCELLED);
}
std::filesystem::path EditorChildProcess::PowerShell() {
    wchar_t directory[MAX_PATH]{};
    GetSystemDirectoryW(directory, MAX_PATH);
    return std::filesystem::path(directory) / "WindowsPowerShell/v1.0/powershell.exe";
}
bool EditorChildProcess::Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                               const std::filesystem::path& directory, const std::filesystem::path& log,
                               std::string& message, const EditorChildProcessOptions& options) {
    if (Running()) {
        message = "This worker is still running.";
        return false;
    }
    state_ = std::make_unique<State>();
    std::wstring command;
    PROJECT_PROCESS::AppendWindowsArgument(command, executable.wstring());
    for (const auto& argument : arguments)
        PROJECT_PROCESS::AppendWindowsArgument(command, argument);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const auto openLog = [&security](const std::filesystem::path& path) {
        return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    HANDLE output = openLog(log);
    HANDLE errorOutput = options.errorLog.empty() ? INVALID_HANDLE_VALUE : openLog(options.errorLog);
    // 標準入力は管道（書き側は親だけが持つ）か NUL。
    HANDLE inputStream = INVALID_HANDLE_VALUE;
    HANDLE inputWriter = nullptr;
    if (options.writableInput) {
        if (!CreatePipe(&inputStream, &inputWriter, &security, 0) ||
            !SetHandleInformation(inputWriter, HANDLE_FLAG_INHERIT, 0))
            inputStream = INVALID_HANDLE_VALUE;
    } else {
        inputStream = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    struct CloseStreams {
        HANDLE written, errors, read, writer;
        ~CloseStreams() {
            for (HANDLE handle : {written, errors, read})
                if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
                    CloseHandle(handle);
            if (writer)
                CloseHandle(writer);
        }
    } closeStreams{output, errorOutput, inputStream, inputWriter};
    if (output == INVALID_HANDLE_VALUE || inputStream == INVALID_HANDLE_VALUE ||
        (!options.errorLog.empty() && errorOutput == INVALID_HANDLE_VALUE)) {
        message = "Cannot open worker streams.";
        return false;
    }
    state_->job = CreateJobObjectW(nullptr, nullptr);
    if (!state_->job) {
        message = "Cannot own the worker process tree.";
        return false;
    }
    if (options.killOnClose) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(state_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            message = "Cannot configure worker lifetime.";
            return false;
        }
    }
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::byte> attributes(attributeBytes);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) {
        message = "Cannot initialize worker handles.";
        return false;
    }
    struct DeleteAttributes {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~DeleteAttributes() { DeleteProcThreadAttributeList(list); }
    } deleteAttributes{list};
    HANDLE handles[]{output, inputStream, errorOutput};
    const size_t handleCount = errorOutput == INVALID_HANDLE_VALUE ? 2u : 3u;
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, handleCount * sizeof(HANDLE),
                                   nullptr, nullptr)) {
        message = "Cannot isolate inherited worker handles.";
        return false;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = errorOutput == INVALID_HANDLE_VALUE ? output : errorOutput;
    startup.StartupInfo.hStdInput = inputStream;
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
                        environment.size() > 1 ? environment.data() : nullptr, directory.c_str(), &startup.StartupInfo,
                        &info)) {
        message = PROJECT_PROCESS::MakeWindowsError("Cannot start worker", GetLastError());
        return false;
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
    if (resumed == static_cast<DWORD>(-1)) {
        Cancel();
        message = "Cannot resume worker.";
        return false;
    }
    state_->input = std::exchange(closeStreams.writer, nullptr);
    return true;
}
} // namespace HIKARI::EDITOR
