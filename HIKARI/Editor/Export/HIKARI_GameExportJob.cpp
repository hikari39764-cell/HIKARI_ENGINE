#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Export/HIKARI_GameShippingArtifactValidator.h"
#include "Editor/Projects/HIKARI_ProjectProcess.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include <Windows.h>
#include <chrono>
#include <fstream>

namespace HIKARI::EDITOR {
    namespace {
        class ExportProcess final {
        public:
            HANDLE process = nullptr, job = nullptr;
            ~ExportProcess() {
                if (process) CloseHandle(process);
                if (job) CloseHandle(job);
            }
            bool Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                const std::filesystem::path& directory, const std::filesystem::path& log, std::string& message,
                bool killOnClose = false) {
                std::wstring command;
                PROJECT_PROCESS::AppendWindowsArgument(command, executable.wstring());
                for (const auto& argument : arguments) PROJECT_PROCESS::AppendWindowsArgument(command, argument);
                SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
                HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (output == INVALID_HANDLE_VALUE) { message = "Could not open export log."; return false; }
                job = CreateJobObjectW(nullptr, nullptr);
                if (!job) { CloseHandle(output); message = "Could not create export process job."; return false; }
                if (killOnClose) {
                    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                        CloseHandle(output); message = "Could not configure export build job."; return false;
                    }
                }
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdOutput = output; startup.hStdError = output;
                startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                PROCESS_INFORMATION info{};
                std::vector<wchar_t> environment;
                if (auto* inherited = GetEnvironmentStringsW()) {
                    for (const wchar_t* entry = inherited; *entry; entry += wcslen(entry) + 1u) {
                        if (_wcsnicmp(entry, L"PSModulePath=", 13u) != 0)
                            environment.insert(environment.end(), entry, entry + wcslen(entry) + 1u);
                    }
                    FreeEnvironmentStringsW(inherited);
                }
                environment.push_back(L'\0');
                const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                    environment.size() > 1u ? environment.data() : nullptr, directory.c_str(), &startup, &info) != FALSE;
                const auto error = GetLastError();
                CloseHandle(output);
                if (!created) { message = PROJECT_PROCESS::MakeWindowsError("Could not start export process", error); return false; }
                process = info.hProcess;
                if (!AssignProcessToJobObject(job, process)) {
                    TerminateProcess(process, 1); CloseHandle(info.hThread);
                    message = "Could not own export child process."; return false;
                }
                ResumeThread(info.hThread); CloseHandle(info.hThread);
                return true;
            }
            bool Running() const { return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }
            DWORD ExitCode() const { DWORD code = 1; if (process) GetExitCodeProcess(process, &code); return code; }
        };
        nlohmann::json Read(const std::filesystem::path& path) {
            std::ifstream input(path);
            if (!input) return nlohmann::json::object();
            auto value = nlohmann::json::parse(input, nullptr, false);
            return value.is_object() ? value : nlohmann::json::object();
        }
        void Write(const std::filesystem::path& path, const nlohmann::json& value) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << value.dump(2);
            if (!output) throw std::runtime_error("Could not write export job state.");
        }
        std::filesystem::path PowerShell() {
            wchar_t directory[MAX_PATH]{};
            GetSystemDirectoryW(directory, MAX_PATH);
            return std::filesystem::path(directory) / "WindowsPowerShell/v1.0/powershell.exe";
        }
        bool PrepareShipping(const std::filesystem::path& project, const std::filesystem::path& jobRoot,
            std::string& message) {
            const auto sourceRoot = ResolveExportEngineSourceRoot();
            std::filesystem::path script;
            std::vector<std::wstring> arguments{L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File"};
            if (!sourceRoot.empty()) {
                script = sourceRoot / "Tools/Build/Build-Hikari.ps1";
                arguments.insert(arguments.end(), {script.wstring(), L"-Preset", L"game-shipping",
                    L"-Project", project.wstring(), L"-Install"});
            } else {
                const auto runtime = PLATFORM::GetExecutableDirectory() / "Runtime/Shipping";
                const auto sdk = runtime / "Sdk";
                script = sdk / "Tools/Build-HikariProject.ps1";
                arguments.insert(arguments.end(), {script.wstring(), L"-Project", project.wstring(),
                    L"-Preset", L"game-shipping", L"-SdkRoot", sdk.wstring(), L"-PrepareGameExport"});
            }
            if (!std::filesystem::is_regular_file(script)) {
                message = "Paired Shipping runtime kit is missing. Reinstall the complete Editor distribution (Runtime/Shipping).";
                return false;
            }
            ExportProcess build;
            if (!build.Start(PowerShell(), arguments, project, jobRoot / "shipping-build.log", message, true)) return false;
            while (build.Running()) {
                if (std::filesystem::exists(jobRoot / "cancel")) {
                    message = "Export cancelled during Shipping build. The previous package was preserved.";
                    return false;
                }
                WaitForSingleObject(build.process, 100);
            }
            if (build.ExitCode() != 0) {
                message = "Shipping build failed. See " + TEXT::PathToUtf8(jobRoot / "shipping-build.log");
                return false;
            }
            return true;
        }
    }
    struct GameExportJob::State {
        std::unique_ptr<ExportProcess> process;
        std::filesystem::path root;
        nlohmann::json status = {{"running", false}, {"message", ""}, {"progress", 0.0f}};
    };
    GameExportJob::GameExportJob() : state_(std::make_unique<State>()) {}
    GameExportJob::~GameExportJob() { Cancel(); }
    bool GameExportJob::IsRunning() const { return state_->process && state_->process->Running(); }
    const nlohmann::json& GameExportJob::Status() const { return state_->status; }
    bool GameExportJob::Start(const GameExportOptions& options, std::string& message) {
        Poll();
        if (IsRunning()) { message = "An export is already running."; return false; }
        if (!GetProjectContext().IsOpen()) { message = "Open a project before exporting."; return false; }
        try {
            const auto project = GetProjectContext().GetRoot();
            state_->root = project / "Library/Build/Exports" /
                std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
            std::filesystem::create_directories(state_->root);
            const auto output = options.outputDirectory.empty() ? GameExporter::GetDefaultOutputDirectory() : options.outputDirectory;
            GameExportIdentity identity;
            if (options.identity) identity = *options.identity;
            else if (!LoadGameExportIdentity(project, identity, message)) return false;
            if (!ValidateGameExportIdentity(project, identity, message)) return false;
            Write(state_->root / "request.json", {{"outputDirectory", TEXT::PathToUtf8(output)},
                {"sceneGuids", options.sceneGuids}, {"startupSceneGuid", options.startupSceneGuid},
                {"windowMode", static_cast<int>(options.windowMode)}, {"prepareShipping", true},
                {"identity", GameExportIdentityJson(identity)}});
            auto child = std::make_unique<ExportProcess>();
            if (!child->Start(PLATFORM::GetExecutablePath(), {L"--project", project.wstring(),
                L"--export-request", (state_->root / "request.json").wstring()}, project, state_->root / "export.log", message)) return false;
            state_->process = std::move(child);
            state_->status = {{"running", true}, {"message", "Preparing Shipping export"}, {"progress", 0.0f},
                {"outputDirectory", TEXT::PathToUtf8(output)}, {"logDirectory", TEXT::PathToUtf8(state_->root)}};
            return true;
        } catch (const std::exception& e) { message = e.what(); return false; }
    }
    void GameExportJob::Poll() {
        if (!state_->process) return;
        const auto progress = Read(state_->root / "status.json");
        if (!progress.empty()) state_->status.update(progress);
        state_->status["running"] = IsRunning();
        if (!IsRunning()) {
            const auto result = Read(state_->root / "result.json");
            if (result.empty()) state_->status.update({{"success", false}, {"message", "Export process stopped. Inspect export.log in the log directory."}});
            else state_->status.update(result);
            state_->process.reset();
        }
    }
    void GameExportJob::Cancel() {
        if (!IsRunning()) return;
        std::ofstream marker(state_->root / "cancel");
        marker << "cancel";
        state_->status["message"] = "Cancelling safely; the previous package is preserved.";
    }
    int GameExportJob::RunRequest(const std::filesystem::path& requestPath) {
        const auto root = requestPath.parent_path();
        GameExportResult result;
        try {
            const auto request = Read(requestPath);
            if (!request.contains("outputDirectory")) throw std::runtime_error("Invalid export request.");
            GameExportOptions options;
            if (request.contains("identity")) {
                GameExportIdentity identity;
                if (!ParseGameExportIdentity(request["identity"], identity, result.message) ||
                    !ValidateGameExportIdentity(GetProjectContext().GetRoot(), identity, result.message))
                    throw std::runtime_error(result.message);
                options.identity = std::move(identity);
            }
            options.outputDirectory = TEXT::PathFromUtf8(request.at("outputDirectory").get<std::string>());
            options.sceneGuids = request.value("sceneGuids", std::vector<std::string>{});
            options.startupSceneGuid = request.value("startupSceneGuid", std::string{});
            const int mode = request.value("windowMode", 1);
            if (mode < 0 || mode > 2) throw std::runtime_error("Invalid export window mode.");
            options.windowMode = static_cast<GameExportWindowMode>(mode);
            auto lastReport = std::chrono::steady_clock::time_point{};
            options.progress = [&](const std::string& stage, float progress) {
                if (std::filesystem::exists(root / "cancel")) return false;
                const auto now = std::chrono::steady_clock::now();
                if (now - lastReport > std::chrono::milliseconds(100) || progress >= 0.98f) {
                    Write(root / "status.json", {{"message", stage}, {"progress", progress}});
                    lastReport = now;
                }
                return true;
            };
            options.progress("Building paired Shipping runtime and game module", 0.0f);
            if (!request.value("prepareShipping", true) || PrepareShipping(GetProjectContext().GetRoot(), root, result.message))
                result = GameExporter::Export(options);
        } catch (const std::exception& e) { result.message = e.what(); }
        Write(root / "result.json", {{"success", result.success}, {"message", result.message},
            {"outputDirectory", TEXT::PathToUtf8(result.outputDirectory)}});
        return result.success ? 0 : 4;
    }
}
