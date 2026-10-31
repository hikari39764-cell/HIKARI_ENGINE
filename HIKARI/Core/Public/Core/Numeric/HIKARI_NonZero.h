#pragma once

#include <cstddef>
#include <type_traits>

namespace HIKARI::NUMERIC {

// 0 を「未設定・無効」に予約した版数・世代・連番の次の値。一周しても 0 にはならない。
template <class T>
constexpr T NextNonZero(T value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    return ++value != T{0} ? value : T{1};
}

// counter を次の値へ進め、進めた値を返す。
template <class T>
constexpr T AdvanceNonZero(T& counter) noexcept {
    counter = NextNonZero(counter);
    return counter;
}

// 配列に 0 以外の要素があるか。0 で埋める契約の予約欄の検査に使う。
template <class T, std::size_t Count>
constexpr bool AnyNonZero(const T (&values)[Count]) noexcept {
    for (const T value : values) {
        if (value != T{}) {
            return true;
        }
    }
    return false;
}

} // namespace HIKARI::NUMERIC
