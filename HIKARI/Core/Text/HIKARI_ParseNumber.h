#pragma once

#include <charconv>
#include <string_view>
#include <system_error>

namespace HIKARI::TEXT {

// 文字列全体が一つの数値のときだけ true。空白や余りがあれば false で value は変えない。
template <class T>
bool ParseNumber(std::string_view text, T& value) noexcept {
    T parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return false;
    value = parsed;
    return true;
}

} // namespace HIKARI::TEXT
