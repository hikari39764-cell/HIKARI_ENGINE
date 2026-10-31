#pragma once

#include <filesystem>

namespace HIKARI::PLATFORM {

std::filesystem::path GetExecutablePath();
std::filesystem::path GetExecutableDirectory();

} // namespace HIKARI::PLATFORM
