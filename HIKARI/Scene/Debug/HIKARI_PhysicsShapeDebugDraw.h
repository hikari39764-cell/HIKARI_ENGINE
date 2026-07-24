#pragma once

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::DEBUG {

    void DrawPhysicsShape(
        const MATH::Mat4& bodyWorld,
        const PHYSICS::PhysicsShapeDesc& shape,
        unsigned int color,
        bool xray);

} // namespace HIKARI::DEBUG
