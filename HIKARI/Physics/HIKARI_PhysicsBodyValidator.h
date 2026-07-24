#pragma once

#include <string>
#include <vector>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    struct PhysicsBodyValidationResult {
        bool valid = false;
        PhysicsErrorCode error = PhysicsErrorCode::None;
        std::string message{};
        std::vector<std::string> warnings{};
    };

    PhysicsBodyValidationResult ValidatePhysicsBodyCreateInfo(
        const PhysicsBodyCreateInfo& createInfo);

    const char* ToString(PhysicsErrorCode error) noexcept;
    const char* ToString(PhysicsBodyRuntimeState state) noexcept;
    const char* ToString(PhysicsMotionType motionType) noexcept;

} // namespace HIKARI::PHYSICS
