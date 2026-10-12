#include "Physics/HIKARI_PhysicsPresentation.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::PHYSICS {

PhysicsPose InterpolatePhysicsPose(const PhysicsPose& previous, const PhysicsPose& current, float alpha) noexcept {
    const float safeAlpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 0.0f;
    PhysicsPose result{};
    result.position = previous.position * (1.0f - safeAlpha) + current.position * safeAlpha;
    result.rotation = MATH::NlerpQ(previous.rotation, current.rotation, safeAlpha);
    return result;
}

} // namespace HIKARI::PHYSICS
