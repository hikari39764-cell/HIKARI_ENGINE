#include "Editor/Projects/HIKARI_ProjectCodeJob.h"
#include "Editor/Projects/HIKARI_ProjectCodeWorkspace.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Project/HIKARI_ProjectContext.h"

#include <Windows.h>
#include <chrono>
#include <fstream>

namespace HIKARI::EDITOR {
ProjectCodeJob::ProjectCodeJob() = default;
ProjectCodeJob::~ProjectCodeJob() = default;
std::filesystem::path ProjectCodeJob::SdkRoot() { return PLATFORM::GetExecutablePath().parent_path() / "Sdk"; }
bool ProjectCodeJob::Running() const { return process_ && process_->Running(); }

bool ProjectCodeJob::Start(std::string& message) {
    Poll();
    if (Running()) {
        message = "A project build is already running.";
        return false;
    }
    if (!GetProjectContext().IsDescriptorBacked()) {
        message = "Open a normal game project first.";
        return false;
    }
    const auto sdk = SdkRoot();
    const auto worker = sdk / "Tools/Invoke-HikariProjectCodeWorker.ps1";
    if (!std::filesystem::is_regular_file(worker)) {
        message = "The installed Editor SDK is missing its project build worker.";
        return false;
    }
    try {
        const auto project = std::filesystem::absolute(GetProjectContext().GetRoot());
        const auto id = std::to_string(GetCurrentProcessId()) + "-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        job_ = project / "Library/Development" / id;
        (void)PROJECT_CODE::PrepareBuild(project, sdk, job_);
        cancelled_ = false;
        auto child = std::make_unique<EditorChildProcess>();
        if (!child->Start(EditorChildProcess::PowerShell(),
                          {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File", worker.wstring(), L"-RequestPath",
                           (job_ / "request.json").wstring()},
                          job_, job_ / "worker.log", message)) {
            status_ = {{"running", false}, {"state", "Failed"}, {"success", false}, {"message", message}};
            return false;
        }
        process_ = std::move(child);
        status_ = {{"running", true},
                   {"state", "Building"},
                   {"success", false},
                   {"taskId", id},
                   {"project", TEXT::PathToUtf8(project)},
                   {"jobDirectory", TEXT::PathToUtf8(job_)},
                   {"build", "Running"},
                   {"behavior", "NotRun"},
                   {"message", "Building a copy of the current project source. Loaded modules are unchanged."}};
        message = status_["message"].get<std::string>();
        return true;
    } catch (const std::exception& error) {
        status_ = {{"running", false}, {"state", "Failed"}, {"success", false}, {"message", error.what()}};
        message = error.what();
        return false;
    }
}
void ProjectCodeJob::Poll() {
    if (!process_)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (Running() && now - lastPoll_ < std::chrono::milliseconds(100))
        return;
    lastPoll_ = now;
    std::ifstream input(job_ / "result.json");
    const auto result = nlohmann::json::parse(input, nullptr, false);
    if (result.is_object()) {
        for (const auto* field : {"build", "behavior", "message"}) {
            if (result.contains(field) && result[field].is_string())
                status_[field] = result[field];
        }
    }
    status_["running"] = Running();
    if (Running())
        return;
    const bool passed = !cancelled_ && process_->ExitCode() == 0 && result.is_object() && result.contains("success") &&
                        result["success"].is_boolean() && result["success"].get<bool>() && result.contains("build") &&
                        result["build"] == "Passed";
    status_["success"] = passed;
    status_["state"] = cancelled_ ? "Cancelled" : passed ? "Built" : "Failed";
    if (cancelled_)
        status_["message"] = "Build cancelled. Original source and the loaded modules are unchanged.";
    else if (!passed && !result.is_object())
        status_["message"] = "Worker failed before reporting a result. Inspect worker.log.";
    process_.reset();
}
void ProjectCodeJob::Cancel() {
    cancelled_ = true;
    if (process_) {
        process_->Cancel();
        process_->Wait(500);
    }
    Poll();
    status_["state"] = "Cancelled";
    status_["success"] = false;
}
} // namespace HIKARI::EDITOR
