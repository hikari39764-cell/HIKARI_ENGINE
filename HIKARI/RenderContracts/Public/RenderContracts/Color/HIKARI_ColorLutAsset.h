#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

// 3D の色の参照表（.cube、Adobe / DaVinci Resolve の形式）。表示の符号化した色を [domainMin, domainMax] で受け、
// 赤が最も速く変わる順に size^3 個の出力色を持つ。1D の表は扱わない。
namespace HIKARI::ASSETS::COLOR {

inline constexpr uint32_t kMinColorLutSize = 2u;
inline constexpr uint32_t kMaxColorLutSize = 128u;

struct ColorLutAsset {
    std::string title;
    uint32_t size = 0u;
    MATH::Vec3 domainMin{0.0f, 0.0f, 0.0f};
    MATH::Vec3 domainMax{1.0f, 1.0f, 1.0f};
    std::vector<MATH::Vec3> table;
};

bool LoadColorLutAsset(const std::filesystem::path& path, ColorLutAsset& out, std::string& error);
// 表を三線形補間で引く。定義域の外は端の値。
MATH::Vec3 SampleColorLut(const ColorLutAsset& lut, const MATH::Vec3& color) noexcept;

} // namespace HIKARI::ASSETS::COLOR
