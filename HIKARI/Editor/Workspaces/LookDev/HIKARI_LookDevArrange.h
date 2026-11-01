#pragma once

#include <cstdint>
#include <vector>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"

// 布景の「整理」：選んだ物の揃え・等間隔・落地と、阵列・镜像の複製の配置。世界の外接箱から世界の移動量を求めるだけで、
// 文書の書き換えは編集器の操作が行う。
namespace HIKARI::EDITOR {

struct LookDevArrangeItem {
    uint64_t id = 0;
    Bounds bounds{};
};

// scene.arrange の action。
enum class LookDevArrangeAction : uint8_t { Align, Distribute, Drop, Group, Mirror, Array };

inline constexpr TEXT::EnumName<LookDevArrangeAction> kLookDevArrangeActionNames[] = {
    {LookDevArrangeAction::Align, "align"},   {LookDevArrangeAction::Distribute, "distribute"},
    {LookDevArrangeAction::Drop, "drop"},     {LookDevArrangeAction::Group, "group"},
    {LookDevArrangeAction::Mirror, "mirror"}, {LookDevArrangeAction::Array, "array"},
};

// 世界の軸（0 X、1 Y、2 Z）。
inline constexpr TEXT::EnumName<int> kLookDevAxisNames[] = {{0, "x"}, {1, "y"}, {2, "z"}};

enum class LookDevAlign : uint8_t {
    Min,
    Center,
    Max,
};

inline constexpr TEXT::EnumName<LookDevAlign> kLookDevAlignNames[] = {
    {LookDevAlign::Min, "min"},
    {LookDevAlign::Center, "center"},
    {LookDevAlign::Max, "max"},
};

// 基準の物（reference の外接箱）の min・中心・max に、軸 axis（0 X、1 Y、2 Z）で揃える移動量（items と同じ順）。
std::vector<MATH::Vec3> AlignLookDevItems(const std::vector<LookDevArrangeItem>& items, const Bounds& reference,
                                          int axis, LookDevAlign align);

// 軸 axis で中心が等間隔になる移動量。両端の物は動かない（3 つ以上）。
std::vector<MATH::Vec3> DistributeLookDevItems(const std::vector<LookDevArrangeItem>& items, int axis);

enum class LookDevArrayKind : uint8_t {
    Linear,
    Grid,
    Ring,
};

inline constexpr TEXT::EnumName<LookDevArrayKind> kLookDevArrayKindNames[] = {
    {LookDevArrayKind::Linear, "linear"},
    {LookDevArrayKind::Grid, "grid"},
    {LookDevArrayKind::Ring, "ring"},
};

struct LookDevArraySettings {
    LookDevArrayKind kind = LookDevArrayKind::Linear;
    // 線形は count 個、格子は count × rows、環形は円周に count 個（元の物を含む）。
    int count = 4;
    int rows = 2;
    // 線形と格子の間隔（m）。線形は X 方向、格子は X と Z。
    float spacing = 2.0f;
    // 環形の半径（m）。円の中心は元の物から -X へ半径だけ離れた点で、複製は中心を向くように回る。
    float radius = 4.0f;
};

struct LookDevArrayCopy {
    MATH::Vec3 offset{};
    float yawDegrees = 0.0f;
};

// 元の物を除く複製の位置（origin からの世界の移動）と Y 軸まわりの回転。
std::vector<LookDevArrayCopy> BuildLookDevArray(const LookDevArraySettings& settings);

// 平面（中心 center、軸 axis に垂直）に映した位置と、Euler XYZ（度）の向き。
MATH::Vec3 MirrorLookDevPosition(const MATH::Vec3& position, const MATH::Vec3& center, int axis) noexcept;
MATH::Vec3 MirrorLookDevRotation(const MATH::Vec3& eulerDegrees, int axis) noexcept;

// 外接箱の和。空なら無効な箱。
Bounds UnionLookDevBounds(const std::vector<LookDevArrangeItem>& items) noexcept;

} // namespace HIKARI::EDITOR
