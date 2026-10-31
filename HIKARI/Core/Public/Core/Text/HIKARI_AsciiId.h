#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace HIKARI::TEXT {

// Lowercases and keeps only alphanumeric characters, producing a stable
// identifier used by hashing and search normalization.
inline std::string NormalizeAsciiId(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if (std::isalnum(c) != 0) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

inline bool AsciiEqualFold(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::TEXT
