#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace HIKARI::MATH {

// 頂点グリッドの補間位置。格納形式・高さ合成・境界の採用規則は呼び出し側が持つ。
struct GridBilinearSample {
    uint32_t x0, x1, z0, z1;
    float tx, tz;

    static GridBilinearSample Clamped(float x, float z, uint32_t cells) noexcept {
        x = std::isfinite(x) ? std::clamp(x, 0.0f, static_cast<float>(cells)) : 0.0f;
        z = std::isfinite(z) ? std::clamp(z, 0.0f, static_cast<float>(cells)) : 0.0f;
        const auto ix = static_cast<uint32_t>(x), iz = static_cast<uint32_t>(z);
        return {ix,
                (std::min)(ix + 1u, cells),
                iz,
                (std::min)(iz + 1u, cells),
                x - static_cast<float>(ix),
                z - static_cast<float>(iz)};
    }

    template <class ReadVertex>
    float Sample(ReadVertex&& read) const noexcept {
        return std::lerp(std::lerp(read(x0, z0), read(x1, z0), tx), std::lerp(read(x0, z1), read(x1, z1), tx), tz);
    }
};

} // namespace HIKARI::MATH
