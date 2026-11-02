#pragma once

#include <cstdint>

namespace HIKARI::ASSETS::LIGHTING {

inline constexpr uint32_t kSurfaceLightmapBakePolicyVersion = 15u;
inline constexpr uint32_t kSurfaceLightmapUvSet = 1u;
inline constexpr uint32_t kSurfaceLightmapDefaultResolution = 128u;
inline constexpr uint32_t kSurfaceLightmapMinimumAtlasResolution = 128u;
inline constexpr uint32_t kSurfaceLightmapMaximumAtlasResolution = 4096u;
inline constexpr uint32_t kSurfaceLightmapMaximumAtlasPages = 16u;
inline constexpr uint32_t kSurfaceLightmapAtlasGuardTexels = 8u;
inline constexpr uint32_t kSurfaceLightmapMinimumChartPaddingTexels = 8u;
inline constexpr uint32_t kSurfaceLightmapAtlasAlignmentTexels = 8u;
inline constexpr uint32_t kSurfaceLightmapSafeMipCount = 4u;
inline constexpr uint32_t kSurfaceLightmapMinimumAuthoredUvExtent = 32u;
inline constexpr float kSurfaceLightmapDefaultTexelsPerWorldUnit = 16.0f;
// 64 標本では天井や壁に斑が残った（降噪込み）。反弹 2 回では開けた建物でも 1–2 割暗かった。
inline constexpr uint32_t kSurfaceLightmapDefaultSampleCount = 256u;
inline constexpr uint32_t kSurfaceLightmapDefaultBounceCount = 4u;
inline constexpr uint32_t kSurfaceLightmapMaximumBounceCount = 8u;

} // namespace HIKARI::ASSETS::LIGHTING
