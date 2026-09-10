#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D {

inline constexpr uint32_t kScreenReadabilityRegionCount = 4u;

struct ScreenReadabilityWorldFrame {
    uint32_t count = 0u;
    std::array<std::array<MATH::Vec3, 4>, kScreenReadabilityRegionCount> regions{};
};

struct ScreenReadabilityFrame {
    uint32_t count = 0u;
    uint32_t attenuateCopy = 0u;
    float curvature = 0.0f;
    float padding = 0.0f;
    std::array<MATH::Vec4, kScreenReadabilityRegionCount * 2u> corners{};
};
static_assert(sizeof(ScreenReadabilityFrame) == 144u);

inline ScreenReadabilityFrame ProjectScreenReadability(
    const ScreenReadabilityWorldFrame& source,
    const MATH::Mat4& viewProjection) noexcept {
    ScreenReadabilityFrame output{};
    for (uint32_t index = 0u; index < (std::min)(source.count, kScreenReadabilityRegionCount); ++index) {
        std::array<MATH::Vec2, 4> uv{};
        bool valid = true;
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            const auto& p = source.regions[index][corner];
            const auto clip = viewProjection.TransformPoint({p.x, p.y, p.z, 1.0f});
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                clip.w <= .0001f || clip.z < 0.0f || clip.z > clip.w) {
                valid = false;
                break;
            }
            uv[corner] = {clip.x / clip.w * .5f + .5f, .5f - clip.y / clip.w * .5f};
        }
        if (!valid) continue;
        const auto cross = [](MATH::Vec2 a, MATH::Vec2 b) { return a.x * b.y - a.y * b.x; };
        const float area = cross(uv[1] - uv[0], uv[2] - uv[0]) + cross(uv[2] - uv[0], uv[3] - uv[0]);
        if (std::abs(area) < .000001f) continue;
        auto& a = output.corners[output.count * 2u];
        auto& b = output.corners[output.count * 2u + 1u];
        a = {uv[0].x, uv[0].y, uv[1].x, uv[1].y};
        b = {uv[2].x, uv[2].y, uv[3].x, uv[3].y};
        ++output.count;
    }
    return output;
}

}
