#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <json.hpp>

namespace HIKARI::EDITOR::PROJECT_SDK {

nlohmann::json Index(const std::filesystem::path& sdkRoot, std::string_view keyword, size_t offset, size_t limit);
nlohmann::json ReadFile(const std::filesystem::path& sdkRoot, const std::string& relativePath, size_t startLine,
                        size_t maxLines);

} // namespace HIKARI::EDITOR::PROJECT_SDK
