#pragma once

#include <string>
#include <string_view>

namespace HIKARI::TEXT {

    char ToLowerAscii(char value) noexcept;
    void ToLowerAsciiInPlace(std::string& value) noexcept;
    std::string ToLowerAsciiCopy(std::string_view value);

} // namespace HIKARI::TEXT
