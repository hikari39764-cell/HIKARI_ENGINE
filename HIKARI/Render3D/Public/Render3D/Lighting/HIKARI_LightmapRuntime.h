#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightingBakePolicy.h"
#include "RenderContracts/Lighting/HIKARI_LightmapSurfaceContract.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"

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
// 登録が変わるたび（Reset・Register）に増える。光追の場面はこれが変わると hit 記録の光照贴图を書き直す。
uint64_t GetVersion();

} // namespace HIKARI::RENDER3D::LIGHTMAP
