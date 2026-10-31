#pragma once

#include <filesystem>
#include <string_view>

namespace HIKARI::PLATFORM {

std::filesystem::path GetLocalAppDataDirectory();
std::filesystem::path GetHikariGameUserRoot(std::string_view projectName);
void SetHikariRuntimeUserRoot(const std::filesystem::path& root);
std::filesystem::path GetHikariRuntimeUserRoot();

} // namespace HIKARI::PLATFORM
