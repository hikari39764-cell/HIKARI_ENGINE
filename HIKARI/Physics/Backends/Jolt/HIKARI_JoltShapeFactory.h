#pragma once

#include <span>
#include <string>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {

    JPH::ShapeRefC BuildCompoundShape(
        std::span<const PhysicsShapeDesc> shapes,
        std::string* outError = nullptr);

    JPH::ShapeRefC BuildQueryShape(
        const PhysicsShapeDesc& shape,
        std::string* outError = nullptr);

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
