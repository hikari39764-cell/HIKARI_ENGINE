#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D {

// shader へ渡す保護の数と、そのために集める候補の数。候補は結んだ順に集め、投影した時に
// このカメラで大きく見える順に選ぶ（結んだ順の先頭だけを保護すると、手前の画面が外れることがある）。
// 画面が多く並ぶ場面で上限を超えると、カメラが動くたびに保護される画面が入れ替わって色調が跳ぶので 12 まで持つ。
// 候補は画面が数十枚並ぶ場面（編集画面で全部の画面を結ぶ時など）でも全部を比べられるよう 128 まで集める
// （32 では結んだ順の後ろにある、目の前の画面が候補から漏れて保護されなかった）。
inline constexpr uint32_t kScreenReadabilityRegionCount = 12u;
inline constexpr uint32_t kScreenReadabilityCandidateCount = 128u;
// 四隅の画面座標（uv）を 1 語へ詰める範囲。x, y をこの範囲の 16 bit 正規化で上下に詰める（1920 画素で約 0.26 画素刻み）。
// 画面の外へ大きくはみ出した隅は範囲の端へ寄せる。HLSL の HikariUnpackReadabilityCorner と同じ値を使う。
inline constexpr float kScreenReadabilityUvMin = -4.0f;
inline constexpr float kScreenReadabilityUvRange = 9.0f;

struct ScreenReadabilityWorldFrame {
    uint32_t count = 0u;
    std::array<std::array<MATH::Vec3, 4>, kScreenReadabilityCandidateCount> regions{};
};

// root constant（上限 64 語）で渡すので、四隅は 1 隅 1 語に詰める（領域ごとに 4 語）。nearDepth は領域ごとの画面の一番
// 手前の隅の 1 - 深度（16 bit 正規化、2 領域で 1 語、HLSL の uint4 に合わせて 8 語）。後処理はその画素の面がこれより
// 手前なら画面の前に物があるとみなして保護しない（前の物に画面の保護が透けない）。
struct ScreenReadabilityFrame {
    uint32_t count = 0u;
    uint32_t attenuateCopy = 0u;
    float curvature = 0.0f;
    float padding = 0.0f;
    std::array<uint32_t, kScreenReadabilityRegionCount * 4u> corners{};
    std::array<uint32_t, 8u> nearDepth{};
};
static_assert(kScreenReadabilityRegionCount <= 16u);
static_assert(sizeof(ScreenReadabilityFrame) == 16u + kScreenReadabilityRegionCount * 16u + 32u);
inline constexpr uint32_t kScreenReadabilityConstantCount = sizeof(ScreenReadabilityFrame) / sizeof(uint32_t);

inline uint32_t PackScreenReadabilityCorner(MATH::Vec2 uv) noexcept {
    const auto quantize = [](float value) {
        const float unit = std::clamp((value - kScreenReadabilityUvMin) / kScreenReadabilityUvRange, 0.0f, 1.0f);
        return static_cast<uint32_t>(std::lround(unit * 65535.0f));
    };
    return quantize(uv.x) | (quantize(uv.y) << 16u);
}

inline ScreenReadabilityFrame ProjectScreenReadability(const ScreenReadabilityWorldFrame& source,
                                                       const MATH::Mat4& viewProjection) noexcept {
    // 候補ごとに四隅の画面座標と、画面に入っている広さ（外接矩形を画面で切った面積）を求める。
    struct Candidate {
        std::array<MATH::Vec2, 4> uv{};
        float visible = 0.0f;
        float nearInverseDepth = 0.0f;
    };
    std::array<Candidate, kScreenReadabilityCandidateCount> candidates{};
    uint32_t candidateCount = 0u;
    for (uint32_t index = 0u; index < (std::min)(source.count, kScreenReadabilityCandidateCount); ++index) {
        std::array<MATH::Vec2, 4> uv{};
        bool valid = true;
        float nearInverseDepth = 0.0f;
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            const auto& p = source.regions[index][corner];
            const auto clip = viewProjection.TransformPoint({p.x, p.y, p.z, 1.0f});
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                clip.w <= .0001f || clip.z < 0.0f || clip.z > clip.w) {
                valid = false;
                break;
            }
            uv[corner] = {clip.x / clip.w * .5f + .5f, .5f - clip.y / clip.w * .5f};
            nearInverseDepth = (std::max)(nearInverseDepth, 1.0f - clip.z / clip.w);
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
        candidates[candidateCount++] = {uv, visible, nearInverseDepth};
    }
    const uint32_t selected = (std::min)(candidateCount, kScreenReadabilityRegionCount);
    std::partial_sort(candidates.begin(), candidates.begin() + selected, candidates.begin() + candidateCount,
                      [](const Candidate& a, const Candidate& b) { return a.visible > b.visible; });
    ScreenReadabilityFrame output{};
    for (uint32_t index = 0u; index < selected; ++index) {
        const auto& uv = candidates[index].uv;
        for (uint32_t corner = 0u; corner < 4u; ++corner)
            output.corners[index * 4u + corner] = PackScreenReadabilityCorner(uv[corner]);
        const auto depth =
            static_cast<uint32_t>(std::lround(std::clamp(candidates[index].nearInverseDepth, 0.0f, 1.0f) * 65535.0f));
        output.nearDepth[index / 2u] |= depth << (16u * (index % 2u));
    }
    output.count = selected;
    return output;
}

} // namespace HIKARI::RENDER3D
