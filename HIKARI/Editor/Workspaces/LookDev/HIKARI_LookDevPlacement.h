#pragma once

#include <cstdint>
#include <optional>
#include <unordered_set>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

struct ImDrawList;

namespace HIKARI {
class Camera3D;
}

namespace HIKARI::EDITOR {

// 布景で置く物。方块類は寸法を変えた程序化の形、人物は 1.8 m の比例の目安、灯は場面の光源。
enum class LookDevPlaceItem : uint8_t {
    Box,
    Wall,
    Floor,
    Pillar,
    Ramp,
    Stairs,
    Sphere,
    Capsule,
    Person,
    PointLight,
    SpotLight,
    TubeLight,
    RectLight,
};

inline constexpr TEXT::EnumName<LookDevPlaceItem> kLookDevPlaceItemNames[] = {
    {LookDevPlaceItem::Box, "box"},
    {LookDevPlaceItem::Wall, "wall"},
    {LookDevPlaceItem::Floor, "floor"},
    {LookDevPlaceItem::Pillar, "pillar"},
    {LookDevPlaceItem::Ramp, "ramp"},
    {LookDevPlaceItem::Stairs, "stairs"},
    {LookDevPlaceItem::Sphere, "sphere"},
    {LookDevPlaceItem::Capsule, "capsule"},
    {LookDevPlaceItem::Person, "person"},
    {LookDevPlaceItem::PointLight, "pointLight"},
    {LookDevPlaceItem::SpotLight, "spotLight"},
    {LookDevPlaceItem::TubeLight, "tubeLight"},
    {LookDevPlaceItem::RectLight, "rectLight"},
};

// 視口の矩形（画面座標）。
struct LookDevView {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// 世界の点を視口の画面座標へ写す。相機の後ろなら false。
bool ProjectLookDevPoint(const Camera3D& camera, const LookDevView& view, const MATH::Vec3& point,
                         MATH::Vec2& screen) noexcept;
// 世界の行列の位置（行ベクトルの 4 行目）。
inline MATH::Vec3 LookDevTranslation(const MATH::Mat4& world) noexcept {
    return {world.m[3][0], world.m[3][1], world.m[3][2]};
}
// ベクトルの軸 axis（0 X、1 Y、2 Z）の成分。
inline float LookDevAxis(const MATH::Vec3& value, int axis) noexcept {
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}
inline float& LookDevAxis(MATH::Vec3& value, int axis) noexcept {
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}
// direction を向く Euler（度、XYZ）。相機と灯は局所 +Z が前。
MATH::Vec3 LookDevFacingRotation(const MATH::Vec3& direction) noexcept;

bool IsLookDevLight(LookDevPlaceItem item) noexcept;
RENDER3D::LIGHTING::SceneLightType LookDevLightType(LookDevPlaceItem item) noexcept;
// 方块類の形と寸法（m）。
ProceduralMeshSettings LookDevShape(LookDevPlaceItem item) noexcept;

// 視口のカーソルの下で物を置ける面。normal は当たった面の外向き（地面なら上）。
struct LookDevSurfaceHit {
    MATH::Vec3 position{};
    MATH::Vec3 normal{0.0f, 1.0f, 0.0f};
};

// 場面の描画物の外接箱と地面（y = 0）から、視線で最初に当たる面を探す。normalizedX/Y は視口内の 0..1。
std::optional<LookDevSurfaceHit> FindLookDevSurface(const Camera3D& camera, float normalizedX, float normalizedY);

// point の真下で最も高い面の高さ（描画物の外接箱の上面、無ければ地面 0）。人眼高度と落地に使う。
// ignoredObjects の物（落とす物自身）は数えない。
float FindLookDevGroundHeight(const MATH::Vec3& point, const std::unordered_set<uint64_t>& ignoredObjects = {});

struct LookDevPlacementPose {
    MATH::Vec3 position{};
    MATH::Vec3 rotationDegrees{};
    // 外形の目安（方块類は寸法、灯は小さな箱）。視口の下書きに使う。
    MATH::Vec3 extent{};
};

// 面に置く時の位置と向き。方块類は底を面に付け（横の面なら面から離し）、gridStep > 0 なら水平方向を格子へ吸着する。
// 灯は面から少し離し、聚光は面の方を向ける。
LookDevPlacementPose ComputeLookDevPlacement(LookDevPlaceItem item, const LookDevSurfaceHit& hit, float gridStep,
                                             float yawDegrees) noexcept;

// 置く前の下書き（外形の線と接地の印）を視口の上に描く。
void DrawLookDevPlacementPreview(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view,
                                 const LookDevPlacementPose& pose, const LookDevSurfaceHit& hit);

} // namespace HIKARI::EDITOR
