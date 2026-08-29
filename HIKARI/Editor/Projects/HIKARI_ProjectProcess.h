#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR::PROJECT_PROCESS {

    void AppendWindowsArgument(
        std::wstring& commandLine,
        std::wstring_view argument);

    std::string MakeWindowsError(
        std::string_view action,
        unsigned long error);

    bool LaunchProcess(
        const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments,
        const std::filesystem::path& workingDirectory,
        std::string_view action,
        std::string& outMessage);

} // namespace HIKARI::EDITOR::PROJECT_PROCESS
