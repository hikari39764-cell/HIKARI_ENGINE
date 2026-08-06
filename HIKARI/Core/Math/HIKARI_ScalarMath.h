#pragma once

namespace HIKARI::MATH {

    inline float Clamp01(float value) noexcept {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

} // namespace HIKARI::MATH
