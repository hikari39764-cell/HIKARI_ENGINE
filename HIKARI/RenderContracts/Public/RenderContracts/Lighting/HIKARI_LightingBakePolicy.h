#pragma once

#include <cstdint>

namespace HIKARI::ASSETS::LIGHTING {

// 16: 路径の反射を PBR（拡散 + 鏡面）にした。
inline constexpr uint32_t kSurfaceLightmapBakePolicyVersion = 16u;
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
// 64 標本では天井や壁に斑が残った（降噪込み）。反弹 2 回では開けた建物でも 1–2 割暗かった。4 回でも明るい室内は路径追跡の
// 参照より 1 割余り暗く（Classroom 0.84）、8 回で 0.91。2 回目から俄羅斯輪盤なので焼く時間はほぼ変わらない（40 秒）。
inline constexpr uint32_t kSurfaceLightmapDefaultSampleCount = 256u;
inline constexpr uint32_t kSurfaceLightmapDefaultBounceCount = 8u;
inline constexpr uint32_t kSurfaceLightmapMaximumBounceCount = 12u;

} // namespace HIKARI::ASSETS::LIGHTING
