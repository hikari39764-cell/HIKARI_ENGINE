#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D::LIGHTMAP {

    constexpr uint32_t kInvalidLightmapTextureDescriptorIndex =
        0xffffffffu;

    struct LightmapRuntimeBinding {
        uint64_t objectId = 0u;
        std::string recordId{};
        std::string sourcePath{};
        TextureResourceHandle texture{};
        uint32_t textureDescriptorIndex =
            kInvalidLightmapTextureDescriptorIndex;
        uint32_t uvSet =
            ASSETS::LIGHTING::kSurfaceLightmapUvSet;
    };

    void Reset();
    bool Register(LightmapRuntimeBinding binding);
    const LightmapRuntimeBinding* Find(uint64_t objectId);
    const std::vector<LightmapRuntimeBinding>& GetBindings();

} // namespace HIKARI::RENDER3D::LIGHTMAP
