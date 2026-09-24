#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace HIKARI::EDITOR::FILE_DIALOG {

std::optional<std::filesystem::path> PickFolder(const std::filesystem::path& initialPath = {});

std::optional<std::filesystem::path> PickFile(const std::filesystem::path& initialPath, const std::wstring& filterLabel,
                                              const std::wstring& filterPattern);

} // namespace HIKARI::EDITOR::FILE_DIALOG
