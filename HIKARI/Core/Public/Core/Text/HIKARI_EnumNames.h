#pragma once

#include <array>
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

// 表に載っている値か。範囲外の列挙値の検出に使う。
template <class E, std::size_t N>
constexpr bool IsNamedEnum(const EnumName<E> (&table)[N], E value) noexcept {
    return EnumToName(table, value, nullptr) != nullptr;
}

// 一致した時だけ outValue を書き換える。
template <class E, std::size_t N>
constexpr bool TryParseEnum(const EnumName<E> (&table)[N], std::string_view text, E& outValue) noexcept {
    const std::optional<E> parsed = TryParseEnum(table, text);
    if (parsed) {
        outValue = *parsed;
    }
    return parsed.has_value();
}

// 表の名前を並び順のまま返す。別名を持たない表の選択肢一覧に使う。
template <class E, std::size_t N>
constexpr std::array<std::string_view, N> EnumNameList(const EnumName<E> (&table)[N]) noexcept {
    std::array<std::string_view, N> names{};
    for (std::size_t index = 0; index < N; ++index) {
        names[index] = table[index].name;
    }
    return names;
}

// 表の値を並び順のまま返す。別名を持たない表の全値の一覧に使う。
template <class E, std::size_t N>
constexpr std::array<E, N> EnumValueList(const EnumName<E> (&table)[N]) noexcept {
    std::array<E, N> values{};
    for (std::size_t index = 0; index < N; ++index) {
        values[index] = table[index].value;
    }
    return values;
}

template <class E, std::size_t N>
constexpr E ParseEnum(const EnumName<E> (&table)[N], std::string_view text, E fallback) noexcept {
    return TryParseEnum(table, text).value_or(fallback);
}

} // namespace HIKARI::TEXT
