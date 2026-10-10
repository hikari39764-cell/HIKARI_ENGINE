#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

// 編集器の数値欄。全パネルが同じ操作感になるように、ドラッグの速さ・表示桁・右クリックの操作をここで一つにする。
namespace HIKARI::EDITOR {

// minimum < maximum の時だけ範囲で丸める。step は値が 0 の時と比例しない時の 1 画素あたりの変化量（0 なら範囲の
// 1/1000、範囲が無ければ 0.001）。proportional なら値の大きさに比例させる（明るさ・密度・距離・倍率）。位置や角度の
// ように原点に意味が無い値は false にして一定の速さにする（0 をまたぐ範囲は自動で一定）。
struct NumberRange {
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
    bool proportional = true;
};

namespace DETAIL {
bool DragNumberComponent(const char* label, double& value, const NumberRange& range, const double* defaultValue,
                         bool integer);
bool SliderNumberComponent(const char* label, double& value, double minimum, double maximum, const double* defaultValue,
                           bool integer);

// value を double で編集し、整数なら丸めて戻す。
template <class T, class Edit>
bool EditAsDouble(T& value, const T* defaultValue, const Edit& edit) {
    double edited = static_cast<double>(value);
    const double fallback = defaultValue != nullptr ? static_cast<double>(*defaultValue) : 0.0;
    if (!edit(edited, defaultValue != nullptr ? &fallback : nullptr, std::is_integral_v<T>)) {
        return false;
    }
    if constexpr (std::is_integral_v<T>) {
        edited = std::clamp(std::round(edited), static_cast<double>(std::numeric_limits<T>::lowest()),
                            static_cast<double>(std::numeric_limits<T>::max()));
    }
    value = static_cast<T>(edited);
    return true;
}
} // namespace DETAIL

// 数値のドラッグ欄。label は ImGui と同じく "##" 以降を表示しない。
// 1 画素あたりの変化はドラッグ開始時の値で決め、離すまで変えない（値が増えるほど加速して跳ぶのを防ぐ）。
// Shift で 10 倍、Alt で 1/100、ダブルクリックか Ctrl+クリックで直接入力。表示は有効 4 桁で指数表記を使わない。
// 右クリックで既定値へ戻す（defaultValue があれば）・コピー・貼り付け。欄は一つのグループなので、呼び出し後の
// IsItemActive / IsItemDeactivatedAfterEdit は欄全体の状態になる。
// 物理量（照度・距離・密度・EV）や、使う値が範囲の一端に寄る量はこの欄にする。
template <class T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
bool DragNumber(const char* label, T& value, const NumberRange& range = {}, const T* defaultValue = nullptr) {
    return DETAIL::EditAsDouble(value, defaultValue, [&](double& edited, const double* fallback, bool integer) {
        return DETAIL::DragNumberComponent(label, edited, range, fallback, integer);
    });
}

// 範囲の滑块。全域に意味がある量（0..1 の強さや混合、角度、± の偏り、色温度）に使う。クリックした位置へ動き、
// Ctrl+クリックで直接入力。表示桁は範囲の 1/1000 が見える桁で固定する。右クリックの操作は DragNumber と同じ。
// minimum < maximum でなければ DragNumber と同じ欄になる。
template <class T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
bool SliderNumber(const char* label, T& value, double minimum, double maximum, const T* defaultValue = nullptr) {
    return DETAIL::EditAsDouble(value, defaultValue, [&](double& edited, const double* fallback, bool integer) {
        return DETAIL::SliderNumberComponent(label, edited, minimum, maximum, fallback, integer);
    });
}

// 成分ごとの欄を横に並べる（Vec2/3/4）。各成分が上の DragNumber と同じ操作を持つ。
bool DragNumbers(const char* label, float* values, int count, const NumberRange& range = {},
                 const float* defaults = nullptr);

} // namespace HIKARI::EDITOR

#endif
