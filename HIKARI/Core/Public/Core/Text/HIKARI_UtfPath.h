#pragma once

#include <filesystem>
#include <string_view>
#include <string>

namespace HIKARI::TEXT {

inline bool IsWindowsReservedFileName(std::string_view name) noexcept {
    const size_t extension = name.find('.');
    std::string_view stem = name.substr(0u, extension);
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
        stem.remove_suffix(1u);
    }
    const auto equalsAsciiInsensitive = [](std::string_view lhs, std::string_view rhs) noexcept {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (size_t index = 0u; index < lhs.size(); ++index) {
            const char left =
                lhs[index] >= 'a' && lhs[index] <= 'z' ? static_cast<char>(lhs[index] - 'a' + 'A') : lhs[index];
            const char right =
                rhs[index] >= 'a' && rhs[index] <= 'z' ? static_cast<char>(rhs[index] - 'a' + 'A') : rhs[index];
            if (left != right) {
                return false;
            }
        }
        return true;
    };
    if (equalsAsciiInsensitive(stem, "CON") || equalsAsciiInsensitive(stem, "PRN") ||
        equalsAsciiInsensitive(stem, "AUX") || equalsAsciiInsensitive(stem, "NUL")) {
        return true;
    }
    return stem.size() == 4u &&
           (equalsAsciiInsensitive(stem.substr(0u, 3u), "COM") || equalsAsciiInsensitive(stem.substr(0u, 3u), "LPT")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

inline std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
    const std::u8string utf8(reinterpret_cast<const char8_t*>(text.data()),
                             reinterpret_cast<const char8_t*>(text.data() + text.size()));
    return std::filesystem::path(utf8);
}

} // namespace HIKARI::TEXT
