#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::DEBUGLOG {

void PushRenderError(std::string message);
std::vector<std::string> GetRecentRenderErrors(size_t maxCount);
void WriteRenderLogLine(const std::string& message);
void SetRenderLogDirectory(const std::filesystem::path& directory);

} // namespace HIKARI::DEBUGLOG
