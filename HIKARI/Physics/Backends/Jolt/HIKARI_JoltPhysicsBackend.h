#pragma once

#include <memory>

#include "Physics/HIKARI_IPhysicsWorldBackend.h"

namespace HIKARI::PHYSICS {

    // Jolt stays behind HIKARI's backend contract. Scene data and gameplay code
    // never need to include vendor headers.
    std::unique_ptr<IPhysicsWorldBackend> CreateJoltPhysicsBackend();

} // namespace HIKARI::PHYSICS
