#pragma once

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::LIGHTING {

struct LightingProviderViewSegment {
    MATH::Vec3 cameraPosition{};
    MATH::Vec3 focusPosition{};
};

float DistanceSquaredToViewSegment(const LightingProviderViewSegment& view, const MATH::Vec3& point) noexcept;

float EvaluateSphereInfluenceAlongView(const LightingProviderViewSegment& view, const MATH::Vec3& center, float radius,
                                       float blendDistance) noexcept;

float EvaluateBoxInfluenceAlongView(const LightingProviderViewSegment& view, const MATH::Vec3& center,
                                    const MATH::Vec3& size, float blendDistance) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
