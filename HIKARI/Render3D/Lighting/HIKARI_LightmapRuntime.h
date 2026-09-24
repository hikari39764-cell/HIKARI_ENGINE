#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Assets/Lighting/HIKARI_LightmapSurfaceContract.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::LIGHTMAP {

constexpr uint32_t kInvalidLightmapTextureDescriptorIndex = 0xffffffffu;

struct LightmapRuntimeBinding {
    uint64_t objectId = 0u;
    ASSETS::LIGHTING::LightmapSurfaceIdentity surface{};
    std::string recordId{};
    std::string sourcePath{};
    TextureResourceHandle texture{};
    uint32_t textureDescriptorIndex = kInvalidLightmapTextureDescriptorIndex;
    uint32_t uvSet = ASSETS::LIGHTING::kSurfaceLightmapUvSet;
    MATH::Vec4 uvScaleBias{1.0f, 1.0f, 0.0f, 0.0f};
};

void Reset();
bool Register(LightmapRuntimeBinding binding);
const LightmapRuntimeBinding* Find(uint64_t objectId, uint32_t nodeIndex, uint32_t meshIndex, uint32_t primitiveIndex);
const std::vector<LightmapRuntimeBinding>& GetBindings();

} // namespace HIKARI::RENDER3D::LIGHTMAP
