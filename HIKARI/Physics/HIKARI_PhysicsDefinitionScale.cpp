#include "Physics/HIKARI_PhysicsDefinitionScale.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::PHYSICS {
    namespace {
        constexpr float kAbsoluteScaleTolerance = 1.0e-5f;
        constexpr float kRelativeScaleTolerance = 2.0e-6f;
        constexpr float kDefinitionScaleQuantum = 1.0e-5f;

        float ScaleTolerance(float left, float right) noexcept {
            const float magnitude = (std::max)(
                1.0f,
                (std::max)(std::abs(left), std::abs(right)));
            return (std::max)(
                kAbsoluteScaleTolerance,
                magnitude * kRelativeScaleTolerance);
        }

        bool AreEquivalent(float left, float right) noexcept {
            return std::abs(left - right) <=
                ScaleTolerance(left, right);
        }

        float Canonicalize(float value) noexcept {
            return std::round(value / kDefinitionScaleQuantum) *
                kDefinitionScaleQuantum;
        }
    }

    bool ArePhysicsDefinitionScalesEquivalent(
        const MATH::Vec3& left,
        const MATH::Vec3& right) noexcept {
        return AreEquivalent(left.x, right.x) &&
            AreEquivalent(left.y, right.y) &&
            AreEquivalent(left.z, right.z);
    }

    MATH::Vec3 ResolvePhysicsDefinitionScale(
        const MATH::Vec3& measuredScale,
        const MATH::Vec3* retainedScale) noexcept {
        // A world matrix is decomposed again whenever bodies are reconciled.
        // Rotation can introduce tiny scale drift even when authored scale did
        // not change. Reuse the accepted definition inside that tolerance so a
        // runtime pose update never becomes a structural body replacement.
        if (retainedScale != nullptr &&
            ArePhysicsDefinitionScalesEquivalent(
                measuredScale,
                *retainedScale)) {
            return *retainedScale;
        }
        return {
            Canonicalize(measuredScale.x),
            Canonicalize(measuredScale.y),
            Canonicalize(measuredScale.z)
        };
    }

} // namespace HIKARI::PHYSICS
