#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <json.hpp>

namespace HIKARI::EDITOR {
    struct GameExportIdentity {
        std::string executableName = "HIKARI_Game.exe";
        std::string windowTitle;
        std::string iconPath;
    };
    bool ResolveExportExecutableFileName(std::string_view requested, std::filesystem::path& output, std::string& message);
    bool ParseGameExportIdentity(const nlohmann::json& document, GameExportIdentity& identity, std::string& message);
    nlohmann::json GameExportIdentityJson(const GameExportIdentity& identity);
    bool ValidateGameExportIdentity(const std::filesystem::path& projectRoot, const GameExportIdentity& identity, std::string& message);
    bool LoadGameExportIdentity(const std::filesystem::path& projectRoot, GameExportIdentity& identity, std::string& message);
    bool SaveGameExportIdentity(const std::filesystem::path& projectRoot, const GameExportIdentity& identity, std::string& message);
}
