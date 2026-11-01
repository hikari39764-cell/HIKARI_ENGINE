#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightmapAtlasLayout.h"

namespace HIKARI::ASSETS::LIGHTING::INTERNAL {

bool FindSmallestLightmapAtlasAtDensity(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                                        const LightmapAtlasSettings& settings, float texelsPerWorldUnit,
                                        uint32_t& outResolution, std::vector<LightmapAtlasAllocation>& outAllocations,
                                        std::string& outMessage);

bool TryBuildLightmapAtlasAtResolution(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                                       const LightmapAtlasSettings& settings, float texelsPerWorldUnit,
                                       uint32_t resolution, std::vector<LightmapAtlasAllocation>& outAllocations,
                                       std::string& outMessage);

} // namespace HIKARI::ASSETS::LIGHTING::INTERNAL
