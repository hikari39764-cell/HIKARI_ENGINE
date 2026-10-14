#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D {

// shader へ渡す保護の数と、そのために集める候補の数。候補は結んだ順に集め、投影した時に
// このカメラで大きく見える順に選ぶ（結んだ順の先頭だけを保護すると、手前の画面が外れることがある）。
inline constexpr uint32_t kScreenReadabilityRegionCount = 4u;
inline constexpr uint32_t kScreenReadabilityCandidateCount = 32u;

struct ScreenReadabilityWorldFrame {
    uint32_t count = 0u;
    std::array<std::array<MATH::Vec3, 4>, kScreenReadabilityCandidateCount> regions{};
};

struct ScreenReadabilityFrame {
    uint32_t count = 0u;
    uint32_t attenuateCopy = 0u;
    float curvature = 0.0f;
    float padding = 0.0f;
    std::array<MATH::Vec4, kScreenReadabilityRegionCount * 2u> corners{};
};
static_assert(sizeof(ScreenReadabilityFrame) == 144u);

inline ScreenReadabilityFrame ProjectScreenReadability(const ScreenReadabilityWorldFrame& source,
                                                       const MATH::Mat4& viewProjection) noexcept {
    // 候補ごとに四隅の画面座標と、画面に入っている広さ（外接矩形を画面で切った面積）を求める。
    struct Candidate {
        std::array<MATH::Vec2, 4> uv{};
        float visible = 0.0f;
    };
    std::array<Candidate, kScreenReadabilityCandidateCount> candidates{};
    uint32_t candidateCount = 0u;
    for (uint32_t index = 0u; index < (std::min)(source.count, kScreenReadabilityCandidateCount); ++index) {
        std::array<MATH::Vec2, 4> uv{};
        bool valid = true;
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            const auto& p = source.regions[index][corner];
            const auto clip = viewProjection.TransformPoint({p.x, p.y, p.z, 1.0f});
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                clip.w <= .0001f || clip.z < 0.0f || clip.z > clip.w) {
                valid = false;
                break;
            }
            uv[corner] = {clip.x / clip.w * .5f + .5f, .5f - clip.y / clip.w * .5f};
        }
        if (!valid)
            continue;
        const auto cross = [](MATH::Vec2 a, MATH::Vec2 b) { return a.x * b.y - a.y * b.x; };
        const float area = cross(uv[1] - uv[0], uv[2] - uv[0]) + cross(uv[2] - uv[0], uv[3] - uv[0]);
        if (std::abs(area) < .000001f)
            continue;
        float left = 1.0f, top = 1.0f, right = 0.0f, bottom = 0.0f;
        for (const auto& point : uv) {
            left = (std::min)(left, (std::max)(point.x, 0.0f));
            top = (std::min)(top, (std::max)(point.y, 0.0f));
            right = (std::max)(right, (std::min)(point.x, 1.0f));
            bottom = (std::max)(bottom, (std::min)(point.y, 1.0f));
        }
        const float visible = (std::max)(right - left, 0.0f) * (std::max)(bottom - top, 0.0f);
        if (visible <= 0.0f)
            continue;
        candidates[candidateCount++] = {uv, visible};
    }
    const uint32_t selected = (std::min)(candidateCount, kScreenReadabilityRegionCount);
    std::partial_sort(candidates.begin(), candidates.begin() + selected, candidates.begin() + candidateCount,
                      [](const Candidate& a, const Candidate& b) { return a.visible > b.visible; });
    ScreenReadabilityFrame output{};
    for (uint32_t index = 0u; index < selected; ++index) {
        const auto& uv = candidates[index].uv;
        output.corners[index * 2u] = {uv[0].x, uv[0].y, uv[1].x, uv[1].y};
        output.corners[index * 2u + 1u] = {uv[2].x, uv[2].y, uv[3].x, uv[3].y};
    }
    output.count = selected;
    return output;
}

} // namespace HIKARI::RENDER3D
