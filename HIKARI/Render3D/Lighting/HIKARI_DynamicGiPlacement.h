#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace HIKARI::RENDER3D::DYNAMICGI::DETAIL {
// 中央・上下左右・真下の命中高さと水平距離（GPU の focus 出力）。
constexpr uint32_t kFocusRayCount = 6;
constexpr uint32_t kFocusValueCount = kFocusRayCount * 2;
constexpr float kFocusMiss = 1.0e30f;

struct NearFieldPlacement {
    float originY = 0.0f;
    float reach = 0.0f;
};

float ProbeGridOrigin(float center, uint32_t count, float spacing);
NearFieldPlacement PlaceNearField(const std::array<float, kFocusValueCount>& values, float cameraHeight,
                                  uint32_t countY, float spacing,
                                  std::optional<NearFieldPlacement> previous = std::nullopt);
} // namespace HIKARI::RENDER3D::DYNAMICGI::DETAIL
