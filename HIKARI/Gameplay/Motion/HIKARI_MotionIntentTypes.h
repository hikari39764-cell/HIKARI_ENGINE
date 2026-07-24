#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::GAMEPLAY {

    using MotionIntentSourceId = uint64_t;

    inline constexpr MotionIntentSourceId
        kPlayerInputMotionSource = 1u;

    struct MotionIntent {
        // Local control axes: +X is right and +Y is forward.
        MATH::Vec2 move{};
        MATH::Vec2 look{};
        bool jumpPressed = false;
        bool jumpHeld = false;
        bool sprintHeld = false;
    };

} // namespace HIKARI::GAMEPLAY
