#include "Platform/Paths/HIKARI_ExecutablePath.h"

#include <Windows.h>

#include <array>

namespace HIKARI::PLATFORM {

    std::filesystem::path GetExecutablePath() {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 ||
            length >= static_cast<DWORD>(buffer.size())) {
            return {};
        }
        return std::filesystem::path(
            buffer.data(),
            buffer.data() + length).lexically_normal();
    }

    std::filesystem::path GetExecutableDirectory() {
        const std::filesystem::path executablePath =
            GetExecutablePath();
        return executablePath.empty()
            ? std::filesystem::path{}
            : executablePath.parent_path();
    }

} // namespace HIKARI::PLATFORM
