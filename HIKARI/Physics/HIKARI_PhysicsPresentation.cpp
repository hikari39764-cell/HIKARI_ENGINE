#include "Physics/HIKARI_PhysicsPresentation.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::PHYSICS {
    namespace {
        MATH::Quat NlerpShortest(
            const MATH::Quat& from,
            MATH::Quat to,
            float alpha) noexcept {
            const float dot = from.x * to.x + from.y * to.y +
                from.z * to.z + from.w * to.w;
            if (dot < 0.0f) {
                to.x = -to.x;
                to.y = -to.y;
                to.z = -to.z;
                to.w = -to.w;
            }

            MATH::Quat blended{};
            blended.x = from.x * (1.0f - alpha) + to.x * alpha;
            blended.y = from.y * (1.0f - alpha) + to.y * alpha;
            blended.z = from.z * (1.0f - alpha) + to.z * alpha;
            blended.w = from.w * (1.0f - alpha) + to.w * alpha;
            return MATH::NormalizeQ(blended);
        }
    }

    PhysicsPose InterpolatePhysicsPose(
        const PhysicsPose& previous,
        const PhysicsPose& current,
        float alpha) noexcept {
        const float safeAlpha = std::isfinite(alpha)
            ? std::clamp(alpha, 0.0f, 1.0f)
            : 0.0f;
        PhysicsPose result{};
        result.position = previous.position * (1.0f - safeAlpha) +
            current.position * safeAlpha;
        result.rotation = NlerpShortest(
            previous.rotation,
            current.rotation,
            safeAlpha);
        return result;
    }

} // namespace HIKARI::PHYSICS
