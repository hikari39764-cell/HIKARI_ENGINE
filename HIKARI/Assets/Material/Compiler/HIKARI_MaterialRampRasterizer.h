#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL {

inline constexpr uint32_t kMaximumMaterialRamp1DResolution = 2048u;
inline constexpr uint32_t kMaximumMaterialRamp2DResolution = 512u;

struct MaterialRampRasterImage {
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t contentHash = 0u;
    std::vector<uint8_t> rgba8{};
};

// Material Ramp assets are authored as deterministic control points and are
// rasterized at import/runtime boundaries. Pixel shaders only sample the
// resulting texture; they never interpret graph or control-point data.
bool RasterizeMaterialRamp(const MaterialRampData& ramp, MaterialRampRasterImage& outImage, std::string& outError);

} // namespace HIKARI::ASSETS::MATERIAL
