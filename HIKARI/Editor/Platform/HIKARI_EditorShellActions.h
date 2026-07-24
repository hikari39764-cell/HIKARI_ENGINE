#pragma once

#include <filesystem>

namespace HIKARI::EDITOR::SHELL {

bool ShowFileInExplorer(const std::filesystem::path &path);
bool OpenFolderInExplorer(const std::filesystem::path &path);

} // namespace HIKARI::EDITOR::SHELL
