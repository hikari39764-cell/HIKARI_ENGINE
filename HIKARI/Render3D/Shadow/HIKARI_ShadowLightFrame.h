#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::SHADOW {

    struct ShadowLightFrame {
        MATH::Mat4 view{};
        MATH::Mat4 viewProj{};
        MATH::Vec3 anchor{};
        MATH::Vec3 lightPosition{};
        MATH::Vec3 lightDirection{};
        MATH::Vec3 right{};
        MATH::Vec3 up{};
        float anchorGrid = 0.0f;
    };

    uint32_t ResolveShadowResolution(uint32_t resolution);

    float ResolveShadowDepthSpan(
        const SceneEnvironment& environment,
        float orthoSize);

    ShadowLightFrame BuildShadowLightFrame(
        const SceneEnvironment& environment,
        const Camera3D& camera,
        uint32_t resolution);

} // namespace HIKARI::SHADOW
