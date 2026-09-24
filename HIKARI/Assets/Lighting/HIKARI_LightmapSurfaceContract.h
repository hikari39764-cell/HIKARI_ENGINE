#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::LIGHTING {

constexpr uint32_t kInvalidLightmapSurfaceIndex = 0xffffffffu;

struct LightmapSurfaceIdentity {
    uint32_t nodeIndex = kInvalidLightmapSurfaceIndex;
    uint32_t meshIndex = kInvalidLightmapSurfaceIndex;
    uint32_t primitiveIndex = kInvalidLightmapSurfaceIndex;

    bool IsValid() const noexcept {
        return meshIndex != kInvalidLightmapSurfaceIndex && primitiveIndex != kInvalidLightmapSurfaceIndex;
    }
};

struct LightmapSurfaceBinding {
    LightmapSurfaceIdentity surface{};
    MATH::Vec4 uvScaleBias{1.0f, 1.0f, 0.0f, 0.0f};
};

} // namespace HIKARI::ASSETS::LIGHTING
