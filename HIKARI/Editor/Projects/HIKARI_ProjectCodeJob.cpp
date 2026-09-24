#include "Editor/Projects/HIKARI_ProjectCodeJob.h"
#include "Editor/Projects/HIKARI_ProjectCodeWorkspace.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
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

bool ProjectCodeJob::Start(const nlohmann::json& edits, std::string& message) {
    Poll();
    if (Running() || (!prepared_.empty() && !cancelled_ && !applied_ && status_.value("success", false))) {
        message = "Finish or discard the current code candidate before starting another.";
        return false;
    }
    if (!GetProjectContext().IsDescriptorBacked()) {
        message = "Open a normal game project first.";
        return false;
    }
    const auto sdk = SdkRoot();
    const auto worker = sdk / "Tools/Invoke-HikariProjectCodeWorker.ps1";
    if (!std::filesystem::is_regular_file(worker)) {
        message = "The installed Editor SDK is missing its project code worker.";
        return false;
    }
    try {
        project_ = std::filesystem::absolute(GetProjectContext().GetRoot());
        const auto id = std::to_string(GetCurrentProcessId()) + "-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        job_ = project_ / "Library/Development" / id;
        prepared_ = PROJECT_CODE::Prepare(project_, sdk, job_, edits);
        cancelled_ = applied_ = false;
        auto child = std::make_unique<EditorChildProcess>();
        if (!child->Start(EditorChildProcess::PowerShell(),
                          {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File", worker.wstring(), L"-RequestPath",
                           (job_ / "request.json").wstring()},
                          job_, job_ / "worker.log", message)) {
            prepared_ = {};
            status_ = {{"running", false}, {"state", "Failed"}, {"success", false}, {"message", message}};
            return false;
        }
        process_ = std::move(child);
        status_ = {{"running", true},
                   {"state", "Building"},
                   {"success", false},
                   {"taskId", id},
                   {"project", TEXT::PathToUtf8(project_)},
                   {"jobDirectory", TEXT::PathToUtf8(job_)},
                   {"build", "Running"},
                   {"behavior", "NotRun"},
                   {"sources", "Staged"},
                   {"activation", "NotApplied"},
                   {"message", "Building a project-only candidate. Original files and loaded modules are unchanged."}};
        message = status_["message"].get<std::string>();
        return true;
    } catch (const std::exception& error) {
        prepared_ = {};
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
        for (const auto* field : {"build", "behavior", "sources", "activation", "message"}) {
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
    status_["state"] = cancelled_ ? "Cancelled" : passed ? "ReadyForReview" : "Failed";
    if (cancelled_)
        status_["message"] = "Candidate cancelled. Original source and last usable modules are preserved.";
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
    status_["message"] = applied_ ? "Source was already applied; use version control to undo it."
                                  : "Candidate discarded; original source and modules are unchanged.";
}
bool ProjectCodeJob::Apply(std::string& message) {
    Poll();
    if (Running() || cancelled_ || applied_ || !status_.value("success", false)) {
        message = "A completed, successful and unapplied candidate is required.";
        return false;
    }
    if (std::filesystem::absolute(GetProjectContext().GetRoot()) != project_) {
        message = "This code candidate belongs to another project.";
        return false;
    }
    try {
        PROJECT_CODE::Apply(project_, job_, prepared_);
        applied_ = true;
        status_["sources"] = "Applied";
        status_["activation"] = "PendingRestart";
        status_["state"] = "Applied";
        message =
            "Source applied. Save the scene, stop Play, then use Project > Rebuild and Reload to enable the new module.";
        status_["message"] = message;
        (void)SERIALIZATION::JSON::WriteJsonFile(job_ / "applied.json", status_);
        return true;
    } catch (const std::exception& error) {
        message = error.what();
        status_["message"] = message;
        return false;
    }
}
nlohmann::json ProjectCodeJob::Diff(const std::string& relative) const {
    if (prepared_.empty() || !GetProjectContext().IsOpen() ||
        std::filesystem::absolute(GetProjectContext().GetRoot()) != project_)
        throw std::runtime_error("There is no code candidate for the current project.");
    nlohmann::json files = nlohmann::json::array();
    for (const auto& item : prepared_.at("edits")) {
        const auto name = item.get<std::string>();
        if (!relative.empty() && relative != name)
            continue;
        auto current = PROJECT_CODE::ReadFile(project_, name);
        auto candidate = PROJECT_CODE::ReadFile(job_ / "workspace", name);
        const auto expected = prepared_.at("before").value(name, std::string{});
        if (relative.empty()) {
            current.erase("content");
            candidate.erase("content");
        }
        files.push_back({{"path", name},
                         {"baseSha256", expected},
                         {"conflict", !applied_ && current["sha256"] != expected},
                         {"applied", applied_},
                         {"current", std::move(current)},
                         {"candidate", std::move(candidate)}});
    }
    if (!relative.empty() && files.empty())
        throw std::runtime_error("That file is not part of this candidate.");
    return {{"files", files}};
}
} // namespace HIKARI::EDITOR
