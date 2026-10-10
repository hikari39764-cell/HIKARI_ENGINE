#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

struct ImDrawList;

namespace HIKARI::EDITOR {

struct EditorPointerRay;

// 視口で扱う箱の置き場所：世界の中心と、局所の各軸の 1 m 分の世界のベクトル（物体の拡縮を含む）。
struct LookDevBoxFrame {
    MATH::Vec3 center{};
    std::array<MATH::Vec3, 3> axes{};
};

// 物体の世界の行列から。localCenter は物体の局所の点（切り抜きの中心など）。
LookDevBoxFrame LookDevFrameFromWorld(const MATH::Mat4& world, const MATH::Vec3& localCenter = {});

// 箱の面の手柄。片側（引いた面だけ動き反対の面は止まる）か、対称（中心のまわりに揃って広がる）。
struct LookDevShapeHandle {
    uint8_t axis = 0;
    float sign = 1.0f;
    bool symmetric = false;
    MATH::Vec3 position{};
};

// axisMask の軸（bit 0 X、1 Y、2 Z）に手柄を置く。symmetricMask の軸は + 側に一つだけ。
std::vector<LookDevShapeHandle> BuildLookDevBoxHandles(const LookDevBoxFrame& frame, const MATH::Vec3& extent,
                                                       uint8_t axisMask, uint8_t symmetricMask);

struct LookDevBoxResize {
    // 新しい局所の寸法（その軸）と、中心の移動（局所の長さと世界のベクトル）。
    float extent = 0.0f;
    float localShift = 0.0f;
    MATH::Vec3 worldShift{};
};

// 引いている面を光線に最も近い所まで動かす。grid > 0 なら世界の寸法をその刻みへ丸める。
std::optional<LookDevBoxResize> ResizeLookDevBox(const LookDevBoxFrame& frame, float startExtent,
                                                 const LookDevShapeHandle& handle, const EditorPointerRay& ray,
                                                 float grid);

// 箱の線（frame の中心から worldShift だけずらし、局所の寸法 extent）。
void DrawLookDevBox(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view, const LookDevBoxFrame& frame,
                    const MATH::Vec3& extent, const MATH::Vec3& worldShift, uint32_t color);

// 丸い切り抜きの線（軸 axis に沿う長さ length の円柱、両端の円と四本の母線）。
void DrawLookDevCylinder(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view,
                         const LookDevBoxFrame& frame, int axis, float radius, float length, uint32_t color);

// 形の手柄の軸：箱類は三軸の片側、平面は X と Z、円柱と胶囊は Y が片側で X が対称の半径、球は X の対称の直径。
std::vector<LookDevShapeHandle> BuildLookDevShapeHandles(const ProceduralMeshSettings& settings,
                                                         const MATH::Mat4& world);
// 形の軸 axis の寸法を value にした設定（円柱・胶囊・球は直径を共有する軸もまとめて変わる）。
ProceduralMeshSettings WithLookDevShapeExtent(ProceduralMeshSettings settings, int axis, float value);

// 世界の寸法の表示（"2.50 x 1.00 x 3.00 m"）。
std::string FormatLookDevSize(const LookDevBoxFrame& frame, const MATH::Vec3& extent, bool flat);

// 開口の種類。床は四角と丸の穴、壁は門・窓・丸窓（procedural.opening の kind）。
enum class LookDevOpeningKind : uint8_t { Hole, RoundHole, Door, Window, RoundWindow };

inline constexpr TEXT::EnumName<LookDevOpeningKind> kLookDevOpeningKindNames[] = {
    {LookDevOpeningKind::Hole, "hole"},
    {LookDevOpeningKind::RoundHole, "round_hole"},
    {LookDevOpeningKind::Door, "door"},
    {LookDevOpeningKind::Window, "window"},
    {LookDevOpeningKind::RoundWindow, "round_window"},
};

// 一番薄い局所の軸（床は Y、壁は X か Z）。開口はこの軸を貫く。scale は物体の拡縮。
int LookDevThinAxis(const ProceduralMeshSettings& settings, const MATH::Vec3& scale) noexcept;
// 種類の既定の開口（局所）。門は 1.0 x 2.1 m で床から、窓は 1.2 x 1.2 m で窓台 0.9 m、穴は 2 m 角、丸は直径
// 1.6 m（丸窓は 1.0 m で窓台 1.0 m）。面の 6 割（高さは 9 割）より大きくしない。
ProceduralMeshCut BuildLookDevOpening(const ProceduralMeshSettings& settings, const MATH::Vec3& scale,
                                      LookDevOpeningKind kind) noexcept;

} // namespace HIKARI::EDITOR
