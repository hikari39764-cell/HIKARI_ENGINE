#include "Core/Paths/HIKARI_UserDataPath.h"

#include <Windows.h>

#include <array>
#include <mutex>
#include <string>

namespace HIKARI::PLATFORM {
namespace {

std::mutex gRuntimeUserRootMutex{};
std::filesystem::path gRuntimeUserRoot{};

std::filesystem::path ReadEnvironmentPath(const wchar_t* name) {
    std::array<wchar_t, 32768u> buffer{};
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0u || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(buffer.data(), buffer.data() + length).lexically_normal();
}

std::string SafeProjectDirectory(std::string_view name) {
    std::string result{};
    result.reserve(name.size());
    for (const unsigned char value : name) {
        const bool alphaNumeric =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
        if (alphaNumeric || value == '-' || value == '_') {
            result.push_back(static_cast<char>(value));
        } else if (result.empty() || result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? "Project" : result;
}

} // namespace

std::filesystem::path GetLocalAppDataDirectory() {
    std::filesystem::path result = ReadEnvironmentPath(L"LOCALAPPDATA");
    if (result.empty()) {
        result = ReadEnvironmentPath(L"APPDATA");
    }
    return result;
}

std::filesystem::path GetHikariGameUserRoot(std::string_view projectName) {
    const std::filesystem::path base = GetLocalAppDataDirectory();
    return base.empty() ? std::filesystem::path{}
                        : (base / "HIKARI" / "Games" / SafeProjectDirectory(projectName)).lexically_normal();
}

void SetHikariRuntimeUserRoot(const std::filesystem::path& root) {
    std::scoped_lock lock(gRuntimeUserRootMutex);
    gRuntimeUserRoot = root.lexically_normal();
}

std::filesystem::path GetHikariRuntimeUserRoot() {
    std::scoped_lock lock(gRuntimeUserRootMutex);
    return gRuntimeUserRoot;
}

} // namespace HIKARI::PLATFORM
