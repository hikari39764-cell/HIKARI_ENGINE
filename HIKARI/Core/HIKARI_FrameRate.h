#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace HIKARI::PERFORMANCE {

// FPS の唯一の計算。frames 枚を seconds 秒で描いた区間が測れていなければ nullopt を返す。
// 呼び出し側は、0 FPS ではなく「データなし」として扱う。
inline std::optional<double> FramesPerSecond(uint64_t frames, double seconds) {
    if (frames == 0u || !std::isfinite(seconds) || seconds <= 0.0) {
        return std::nullopt;
    }
    return static_cast<double>(frames) / seconds;
}

// 表示用の文字列。測れていなければ "no data"。
inline std::string FormatFramesPerSecond(std::optional<double> fps) {
    if (!fps) {
        return "no data";
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.1f", *fps);
    return text;
}

} // namespace HIKARI::PERFORMANCE
