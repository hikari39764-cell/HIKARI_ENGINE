#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace HIKARI::TEXT {

inline std::string TrimCopy(std::string value) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

} // namespace HIKARI::TEXT
