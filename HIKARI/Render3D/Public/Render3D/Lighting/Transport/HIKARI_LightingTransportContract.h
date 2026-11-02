#pragma once

#include <cstdint>

#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"

namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT {

struct LightingTransportBakeSettings {
    uint32_t sampleCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultSampleCount;
    uint32_t bounceCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultBounceCount;
    uint32_t outputResolution = ASSETS::LIGHTING::kSurfaceLightmapDefaultResolution;
};

} // namespace HIKARI::RENDER3D::LIGHTING::TRANSPORT
