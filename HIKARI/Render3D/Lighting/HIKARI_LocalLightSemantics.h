#pragma once

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
    struct PointLight;
    struct SpotLight;
}

namespace HIKARI::RENDER3D::LIGHTING {

    struct SpotLightCone {
        float innerAngleDegrees = 25.0f;
        float outerAngleDegrees = 35.0f;
        float innerCos = 0.0f;
        float outerCos = 0.0f;
        float angularFalloff = 1.0f;
    };

    bool IsLocalLightUploadable(const PointLight& light) noexcept;
    bool IsLocalLightUploadable(const SpotLight& light) noexcept;

    MATH::Vec3 ResolveSpotLightDirection(
        const SpotLight& light) noexcept;
    SpotLightCone ResolveSpotLightCone(
        const SpotLight& light) noexcept;
    void SanitizeSpotLight(SpotLight& light) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
