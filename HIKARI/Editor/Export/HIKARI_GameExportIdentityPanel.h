#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <json.hpp>

namespace HIKARI::EDITOR {
    class EditorOperationRegistry;
    class GameExportIdentityPanel {
    public:
        void Reset();
        void Draw(EditorOperationRegistry& operations, bool busy);
        bool IsSaved() const { return loaded_ && !dirty_; }
    private:
        void SetDocument(const nlohmann::json& document);
        nlohmann::json Draft() const;
        std::array<char, 1024> executable_{};
        std::array<char, 1024> title_{};
        std::array<char, 4096> icon_{};
        nlohmann::json saved_;
        std::filesystem::path root_;
        std::string message_;
        bool loaded_ = false;
        bool dirty_ = false;
    };
}
