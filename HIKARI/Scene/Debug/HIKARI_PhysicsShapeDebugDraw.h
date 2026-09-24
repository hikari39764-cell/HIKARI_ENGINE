#pragma once

#include <cstdint>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::DEBUG {

struct PhysicsShapeDebugStyle {
    uint32_t outlineRgba = 0xFFFFFFFFu;
    uint32_t fillRgba = 0u;
    float thicknessPixels = 1.5f;
    bool xray = false;
};

void DrawPhysicsShape(const MATH::Mat4& bodyWorld, const PHYSICS::PhysicsShapeDesc& shape,
                      const PhysicsShapeDebugStyle& style);

} // namespace HIKARI::DEBUG
