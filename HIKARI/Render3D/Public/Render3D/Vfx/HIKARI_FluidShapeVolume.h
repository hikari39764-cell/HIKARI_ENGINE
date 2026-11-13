#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {
class ModelAsset;
}

// 流体源の形 Model が読む、模型の輪郭の距離の体積。
namespace HIKARI::RENDER3D::FLUID {

// 模型の局所空間の箱 [boundsMin, boundsMax] を cells 個の格に分け、各格の中心に表面までの符号付き距離（内側が負）を持つ。
// 値は距離を band [m] で割って −1〜1 に切り詰め、127 倍して丸めた int8（GPU では R8_SNORM の 3D 貼図）。箱は模型の
// 包围盒を band ぶん外へ広げてあるので、箱の面の上の値はどれも +1（外）。距離が正確なのは表面から band の内側だけで、
// それより遠い格は符号だけが正しい（±1）。並びは x + cells[0] × (y + cells[1] × z)。
struct FluidShapeVolume {
    uint32_t cells[3]{};
    MATH::Vec3 boundsMin{};
    MATH::Vec3 boundsMax{};
    float band = 0.0f;
    std::vector<int8_t> distance;
};

// 体積のいちばん長い辺の格の数の既定と上限。細い剣（長さ 7 m）で 1 格 約 6 cm、体積は 128 × 40 × 12 格ほど。
inline constexpr uint32_t kFluidShapeVolumeCells = 128u;
// 距離を正確に持つ幅（格の数）。源の縁の柔らかさ（域の格 1.5 個ぶん）と填充の境目を、この幅の中で滑らかに読む。
inline constexpr float kFluidShapeVolumeBandCells = 4.0f;

// 模型の全ての網（節点の変換を掛けた静止の姿勢）から体積を焼く。三角形が無ければ空（nullptr）。
// 距離は各三角形の近く（band の内側）の格だけで量り、内外は三つの軸に沿った符号付きの交差の数（巻き数）の多数決で決める：
// 殻が重なった模型（刃と鍔が重なる剣）でも内側は内側になり、小さな穴や裏返った面が少しあっても崩れにくい。
std::shared_ptr<const FluidShapeVolume> BakeFluidShapeVolume(const ModelAsset& model,
                                                             uint32_t longestAxisCells = kFluidShapeVolumeCells);

} // namespace HIKARI::RENDER3D::FLUID
