#pragma once

#include <array>
#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::SHADOW {

    inline constexpr uint32_t kMaxDirectionalShadowCascades = 4u;

    struct ShadowLightFrame {
        MATH::Mat4 view{};
        MATH::Mat4 viewProj{};
        MATH::Vec3 anchor{};
        MATH::Vec3 lightPosition{};
        MATH::Vec3 lightDirection{};
        MATH::Vec3 right{};
        MATH::Vec3 up{};
        float anchorGrid = 0.0f;
        float splitNear = 0.0f;
        float splitFar = 0.0f;
        float orthoSize = 0.0f;
        float lightNear = 0.0f;
        float lightFar = 0.0f;
    };

    struct DirectionalShadowFrameSet {
        std::array<ShadowLightFrame, kMaxDirectionalShadowCascades> cascades{};
        uint32_t cascadeCount = 1u;
        MATH::Vec4 splitDistances{};
    };

    uint32_t ResolveShadowResolution(uint32_t resolution);

    DirectionalShadowFrameSet BuildDirectionalShadowFrameSet(
        const SceneEnvironment& environment,
        const Camera3D& camera,
        uint32_t cascadeResolution);

} // namespace HIKARI::SHADOW
