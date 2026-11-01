#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "RenderContracts/Lighting/HIKARI_SkyMode.h"

namespace HIKARI {

constexpr SkyMode ResolveSkyRenderMode(SkySource source) noexcept {
    switch (source) {
    case SkySource::Hdri: return SkyMode::Cubemap;
    case SkySource::Panorama: return SkyMode::Texture2D;
    case SkySource::Atmosphere:
    default: return SkyMode::Atmosphere;
    }
}

struct SkyAsset {
    std::string name;
    std::string meshAssetName;
    std::string texturePath;
    SkyMode preferredMode = SkyMode::Cubemap;
    bool textureIsLinear = false;
};

} // namespace HIKARI
