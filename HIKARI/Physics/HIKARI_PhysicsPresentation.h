#pragma once

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    PhysicsPose InterpolatePhysicsPose(
        const PhysicsPose& previous,
        const PhysicsPose& current,
        float alpha) noexcept;

} // namespace HIKARI::PHYSICS
