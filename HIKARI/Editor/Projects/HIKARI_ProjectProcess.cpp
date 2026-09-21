#include "Editor/Projects/HIKARI_ProjectProcess.h"

#include <Windows.h>

namespace HIKARI::EDITOR::PROJECT_PROCESS {
namespace {

    std::wstring QuoteWindowsArgument(
        std::wstring_view argument) {

        if (argument.empty()) {
            return L"\"\"";
        }
        if (argument.find_first_of(L" \t\n\v\"") ==
            std::wstring_view::npos) {
            return std::wstring(argument);
        }

        std::wstring quoted{ L'\"' };
        size_t backslashCount = 0u;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashCount;
                continue;
            }
            if (character == L'\"') {
                quoted.append(
                    backslashCount * 2u + 1u,
                    L'\\');
                quoted.push_back(L'\"');
                backslashCount = 0u;
                continue;
            }
            quoted.append(backslashCount, L'\\');
            backslashCount = 0u;
            quoted.push_back(character);
        }
        quoted.append(backslashCount * 2u, L'\\');
        quoted.push_back(L'\"');
        return quoted;
    }

} // namespace

    void AppendWindowsArgument(
        std::wstring& commandLine,
        std::wstring_view argument) {

        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteWindowsArgument(argument);
    }

    std::string MakeWindowsError(
        std::string_view action,
        unsigned long error) {

        return std::string(action) +
            " (Windows error " +
            std::to_string(error) +
            ").";
    }

    bool LaunchProcess(
        const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments,
        const std::filesystem::path& workingDirectory,
        std::string_view action,
        std::string& outMessage, HANDLE* outProcess) {

        std::wstring commandLine{};
        AppendWindowsArgument(
            commandLine,
            executable.wstring());
        for (const std::wstring& argument : arguments) {
            AppendWindowsArgument(commandLine, argument);
        }

        std::vector<wchar_t> mutableCommandLine(
            commandLine.begin(),
            commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation{};
        if (!CreateProcessW(
                executable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NEW_PROCESS_GROUP |
                    CREATE_UNICODE_ENVIRONMENT,
                nullptr,
                workingDirectory.empty()
                    ? nullptr
                    : workingDirectory.c_str(),
                &startupInfo,
                &processInformation)) {
            outMessage = MakeWindowsError(
                action,
                GetLastError());
            return false;
        }

        CloseHandle(processInformation.hThread);
        if (outProcess) *outProcess = processInformation.hProcess;
        else CloseHandle(processInformation.hProcess);
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::EDITOR::PROJECT_PROCESS
