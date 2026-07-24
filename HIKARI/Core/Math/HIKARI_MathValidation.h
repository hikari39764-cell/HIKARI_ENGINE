#pragma once

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::MATH {

    bool IsFinite(float value) noexcept;
    bool IsFinite(const Vec2& value) noexcept;
    bool IsFinite(const Vec3& value) noexcept;
    bool IsFinite(const Vec4& value) noexcept;
    bool IsFinite(const Quat& value) noexcept;

} // namespace HIKARI::MATH
