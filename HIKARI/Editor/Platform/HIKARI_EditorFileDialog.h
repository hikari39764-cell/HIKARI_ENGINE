#pragma once

#include <filesystem>
#include <optional>

namespace HIKARI::EDITOR::FILE_DIALOG {

    std::optional<std::filesystem::path> PickFolder(
        const std::filesystem::path& initialPath = {});

} // namespace HIKARI::EDITOR::FILE_DIALOG
