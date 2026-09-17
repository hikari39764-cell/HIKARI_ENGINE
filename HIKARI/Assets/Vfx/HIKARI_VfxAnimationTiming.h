#pragma once
#include <algorithm>
#include <cmath>

namespace HIKARI::VFX {
// 可視区間とアニメーションの時刻を分離。トリムは位相を保ち、リタイムは速度だけ変える。
struct VfxAnimationTiming {
    float sourceDuration = 0;
    float offset = 0;
    float rate = 1;
    float Duration(float activeDuration) const noexcept {
        return (std::max)(sourceDuration > 0 ? sourceDuration : activeDuration, 0.0001f);
    }
    float Seconds(float layerSeconds, float activeDuration) const noexcept {
        return std::clamp(offset + layerSeconds * rate, 0.0f, Duration(activeDuration));
    }
    float Normalized(float layerSeconds, float activeDuration) const noexcept {
        return Seconds(layerSeconds, activeDuration) / Duration(activeDuration);
    }
    float LayerSeconds(float normalized, float activeDuration) const noexcept {
        return (normalized * Duration(activeDuration) - offset) / rate;
    }
    bool IsValid() const noexcept {
        return std::isfinite(sourceDuration) && sourceDuration >= 0 && sourceDuration <= 86400 &&
               std::isfinite(offset) && std::abs(offset) <= 86400 && std::isfinite(rate) && rate >= 0.0001f &&
               rate <= 1000;
    }
};
} // namespace HIKARI::VFX
