#pragma once

#include <cmath>
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::MATH {
    inline Vec3 BuildFallbackTangent(const Vec3& normal) {
        const Vec3 reference = std::abs(normal.y) < 0.999f
            ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        return Normalize(Cross(reference, normal));
    }
}
