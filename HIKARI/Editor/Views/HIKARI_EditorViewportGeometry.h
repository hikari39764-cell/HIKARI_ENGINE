#pragma once
#include "Core/Math/HIKARI_Math3D.h"
namespace HIKARI {
class Camera3D;
}
namespace HIKARI::EDITOR {
struct EditorPointerRay {
    MATH::Vec3 origin{}, direction{};
};
bool BuildEditorPointerRay(const Camera3D&, float normalizedX, float normalizedY, EditorPointerRay&) noexcept;
bool IntersectEditorBounds(const EditorPointerRay&, const MATH::Vec3& minimum, const MATH::Vec3& maximum,
                           float& distance) noexcept;
// 光線と平面（point を通り normal に垂直）の前方の交点。
bool IntersectEditorPlane(const EditorPointerRay&, const MATH::Vec3& point, const MATH::Vec3& normal,
                          MATH::Vec3& hit) noexcept;
// 直線 origin + s * axis（axis は単位）上で光線に最も近い点の s。光線と平行なら false。
bool ClosestEditorLineParameter(const EditorPointerRay&, const MATH::Vec3& origin, const MATH::Vec3& axis,
                                float& s) noexcept;
} // namespace HIKARI::EDITOR
