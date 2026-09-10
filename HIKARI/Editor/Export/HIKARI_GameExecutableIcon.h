#pragma once
#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {
    bool ValidateGameExecutableIcon(const std::filesystem::path& icon, std::string& message);
    bool EmbedGameExecutableIcon(const std::filesystem::path& executable,
        const std::filesystem::path& icon, std::string& message);
}
