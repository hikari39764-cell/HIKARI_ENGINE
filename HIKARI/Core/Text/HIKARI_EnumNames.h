#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace HIKARI::TEXT {

// 列挙値と名前の対応表。1 つの値に複数の名前（別名）を並べてよく、
// 名前を返す時は表で最初に現れた名前を使う。
template <class E>
struct EnumName {
    E value;
    const char* name;
};

template <class E, std::size_t N>
constexpr const char* EnumToName(const EnumName<E> (&table)[N], E value, const char* fallback = "") noexcept {
    for (const EnumName<E>& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return fallback;
}

template <class E, std::size_t N>
constexpr std::optional<E> TryParseEnum(const EnumName<E> (&table)[N], std::string_view text) noexcept {
    for (const EnumName<E>& entry : table) {
        if (text == entry.name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <class E, std::size_t N>
constexpr E ParseEnum(const EnumName<E> (&table)[N], std::string_view text, E fallback) noexcept {
    return TryParseEnum(table, text).value_or(fallback);
}

} // namespace HIKARI::TEXT
