#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace HIKARI::TEXT {

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
