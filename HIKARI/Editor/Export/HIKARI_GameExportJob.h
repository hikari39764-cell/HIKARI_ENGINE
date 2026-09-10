#pragma once
#include "Editor/Export/HIKARI_GameExporter.h"
#include <memory>
#include <json.hpp>

namespace HIKARI::EDITOR {
    class EditorOperationRegistry;
    class GameExportJob final {
    public:
        GameExportJob();
        ~GameExportJob();
        bool Start(const GameExportOptions& options, std::string& message);
        void Poll();
        void Cancel();
        bool IsRunning() const;
        const nlohmann::json& Status() const;
        static int RunRequest(const std::filesystem::path& requestPath);
    private:
        struct State;
        std::unique_ptr<State> state_;
    };
    void RegisterGameExportOperations(EditorOperationRegistry& registry, GameExportJob& job,
        std::function<bool()> canStart);
}
