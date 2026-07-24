#pragma once

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::PHYSICS {

    bool ArePhysicsDefinitionScalesEquivalent(
        const MATH::Vec3& left,
        const MATH::Vec3& right) noexcept;

    MATH::Vec3 ResolvePhysicsDefinitionScale(
        const MATH::Vec3& measuredScale,
        const MATH::Vec3* retainedScale = nullptr) noexcept;

} // namespace HIKARI::PHYSICS
