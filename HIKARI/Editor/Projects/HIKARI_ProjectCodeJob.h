#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <json.hpp>

namespace HIKARI::EDITOR {
    class EditorChildProcess;
    class EditorOperationRegistry;
    class ProjectCodeJob final {
    public:
        ProjectCodeJob();
        ~ProjectCodeJob();
        bool Start(const nlohmann::json& edits, std::string& message);
        void Poll();
        void Cancel();
        bool Apply(std::string& message);
        nlohmann::json Diff(const std::string& relative = {}) const;
        bool Running() const;
        const nlohmann::json& Status() const { return status_; }
        static std::filesystem::path SdkRoot();
    private:
        std::unique_ptr<EditorChildProcess> process_;
        std::filesystem::path project_, job_;
        nlohmann::json prepared_, status_ = {{"running", false}, {"state", "Idle"}};
        bool cancelled_ = false;
        bool applied_ = false;
        std::chrono::steady_clock::time_point lastPoll_{};
    };
    void RegisterProjectCodeOperations(EditorOperationRegistry&, ProjectCodeJob&, std::function<bool()> canApply);
}
