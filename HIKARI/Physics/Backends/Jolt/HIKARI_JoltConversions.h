#pragma once

#include <Jolt/Jolt.h>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {

    inline JPH::Vec3 ToJolt(const MATH::Vec3& value) noexcept {
        return JPH::Vec3(value.x, value.y, value.z);
    }

    inline JPH::RVec3 ToJoltPosition(const MATH::Vec3& value) noexcept {
        return JPH::RVec3(value.x, value.y, value.z);
    }

    inline JPH::Quat ToJolt(const MATH::Quat& value) noexcept {
        const MATH::Quat normalized = MATH::NormalizeQ(value);
        return JPH::Quat(
            normalized.x,
            normalized.y,
            normalized.z,
            normalized.w);
    }

    inline MATH::Vec3 FromJolt(JPH::Vec3Arg value) noexcept {
        return { value.GetX(), value.GetY(), value.GetZ() };
    }

    inline MATH::Vec3 FromJoltPosition(JPH::RVec3Arg value) noexcept {
        return {
            static_cast<float>(value.GetX()),
            static_cast<float>(value.GetY()),
            static_cast<float>(value.GetZ())
        };
    }

    inline MATH::Quat FromJolt(JPH::QuatArg value) noexcept {
        return {
            value.GetX(),
            value.GetY(),
            value.GetZ(),
            value.GetW()
        };
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
