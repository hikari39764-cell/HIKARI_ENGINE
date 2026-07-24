#pragma once

#include <cstdint>

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::GAMEPLAY {

    struct CharacterMotionState {
        RuntimeObjectHandle object{};
        MATH::Vec3 worldVelocity{};
        MATH::Vec3 horizontalVelocity{};
        MATH::Vec3 groundVelocity{};
        MATH::Vec3 groundNormal{ 0.0f, 1.0f, 0.0f };
        float horizontalSpeed = 0.0f;
        float verticalSpeed = 0.0f;
        float inputMagnitude = 0.0f;
        PHYSICS::PhysicsCharacterGroundState groundState =
            PHYSICS::PhysicsCharacterGroundState::InAir;
        uint64_t fixedTickIndex = 0u;
        bool moving = false;
        bool sprinting = false;
        bool hitWall = false;
        bool hitCeiling = false;

        bool IsGrounded() const noexcept {
            return groundState ==
                PHYSICS::PhysicsCharacterGroundState::OnGround;
        }
    };

} // namespace HIKARI::GAMEPLAY
