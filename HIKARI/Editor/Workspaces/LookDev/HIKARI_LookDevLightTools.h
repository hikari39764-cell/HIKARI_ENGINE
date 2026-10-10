#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"

struct ImDrawList;

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

struct EditorPointerRay;

// 三点照明の一灯（主光・補光・輪郭光）。被写体の中心を向くスポットで、明るさは場面に対する相対。
struct LookDevRigLight {
    std::string_view nameKey;
    MATH::Vec3 position{};
    MATH::Vec3 rotationDegrees{};
    LightSettings settings{};
};

// 被写体（外接箱）を相機から見て、主光は右前の上、補光は左前の低め、輪郭光は後ろの上に置く。
// 被写体の位置で主光が環境の照度と同じ、補光が 3 割、輪郭光が 8 割になるように倍率を決める。
std::array<LookDevRigLight, 3> BuildThreePointLights(const Bounds& subject, const MATH::Vec3& cameraPosition);

// 選んだ灯の視口の調整点。点光は範囲、スポットは届く距離と外側・内側の円錐、面光は幅と高さ、管は長さ。
enum class LookDevLightHandle : uint8_t {
    None,
    Range,
    OuterCone,
    InnerCone,
    Width,
    Height,
    Length,
};

struct LookDevLightHandlePoint {
    LookDevLightHandle handle = LookDevLightHandle::None;
    MATH::Vec3 position{};
};

std::vector<LookDevLightHandlePoint> BuildLookDevLightHandles(const LightSettings& settings, const MATH::Mat4& world,
                                                              const Camera3D& camera);

// ドラッグ中の光線から調整点の値を求めて settings へ書く。変わったら true。
bool ApplyLookDevLightHandle(LookDevLightHandle handle, const LightSettings& before, const MATH::Mat4& world,
                             const EditorPointerRay& ray, const Camera3D& camera, LightSettings& settings);

// 灯の形（点光の範囲の円、スポットの円錐、面光の長方形、管の線）を視口に描く。
void DrawLookDevLightShape(ImDrawList& drawList, const Camera3D& camera, float x, float y, float width, float height,
                           const LightSettings& settings, const MATH::Mat4& world);

} // namespace HIKARI::EDITOR
