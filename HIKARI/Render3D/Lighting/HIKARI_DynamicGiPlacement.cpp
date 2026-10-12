#include "Render3D/Lighting/HIKARI_DynamicGiPlacement.h"

#include <algorithm>
#include <cmath>
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::RENDER3D::DYNAMICGI::DETAIL {
float ProbeGridOrigin(float center, uint32_t count, float spacing) {
    return (std::floor(center / spacing - 0.5f) + 0.5f - float(count / 2)) * spacing;
}

NearFieldPlacement PlaceNearField(const std::array<float, kFocusValueCount>& values, float cameraHeight,
                                  uint32_t countY, float spacing, std::optional<NearFieldPlacement> previous) {
    std::array<float, kFocusRayCount - 1> heights{}, distances{};
    size_t count = 0;
    const auto hit = [&](size_t index) { return MATH::IsFinite(values[index]) && values[index] < kFocusMiss; };
    for (size_t index = 0; index + 1 < kFocusRayCount; ++index)
        if (hit(index)) {
            heights[count] = values[index];
            distances[count++] = values[kFocusRayCount + index];
        }
    const size_t viewCount = count;
    const bool belowHit = hit(kFocusRayCount - 1);
    const float below = values[kFocusRayCount - 1];
    if (count == 0 && belowHit)
        heights[count++] = below;
    NearFieldPlacement placement{ProbeGridOrigin(cameraHeight, countY, spacing), 0.0f};
    if (count == 0)
        return placement;

    const float extent = float(countY - 1) * spacing;
    const float halfHeight = 0.5f * extent;
    std::sort(heights.begin(), heights.begin() + count);
    const float viewHeight = heights[count / 2] + 0.5f * halfHeight;
    float center = (std::min)(cameraHeight, viewHeight);
    if (belowHit && cameraHeight - below < halfHeight)
        center = viewHeight >= cameraHeight ? (std::max)(center, below + halfHeight - 0.5f * spacing)
                                            : (std::max)(center, below - halfHeight + spacing);

    // 真下の壁頂・屋根だけで格子を持ち上げない。見えている面を、縁の一間隔の移行帯より内側へ残す。
    // 足元も同時に収まる時だけ範囲へ含める（崖の上から谷を見る時は視線側を優先）。
    float low = heights[0], high = heights[count - 1];
    if (belowHit && cameraHeight - below < halfHeight &&
        (std::max)(high, below) - (std::min)(low, below) <= extent - 2.0f * spacing) {
        low = (std::min)(low, below);
        high = (std::max)(high, below);
    }
    const float minOrigin = (std::ceil((high + spacing - extent) / spacing - 0.5f) + 0.5f) * spacing;
    const float maxOrigin = (std::floor((low - spacing) / spacing - 0.5f) + 0.5f) * spacing;
    placement.originY = ProbeGridOrigin(center, countY, spacing);
    // 小さな視線の揺れで格子を往復させない。ただし可視面の coverage は履歴より優先する。
    if (previous && std::abs(placement.originY - previous->originY) <= spacing)
        placement.originY = previous->originY;
    if (minOrigin <= maxOrigin)
        placement.originY = std::clamp(placement.originY, minOrigin, maxOrigin);
    if (viewCount > 0) {
        std::sort(distances.begin(), distances.begin() + viewCount);
        const float elevation = belowHit ? (std::max)(cameraHeight - below, 0.0f) : distances[viewCount / 2];
        placement.reach = (std::min)(distances[viewCount / 2], 1.5f * elevation);
        if (previous && std::abs(placement.reach - previous->reach) <= spacing)
            placement.reach = previous->reach;
    }
    return placement;
}
} // namespace HIKARI::RENDER3D::DYNAMICGI::DETAIL
