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
} // namespace HIKARI::EDITOR
