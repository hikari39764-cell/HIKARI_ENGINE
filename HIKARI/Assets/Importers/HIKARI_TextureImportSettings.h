#pragma once

#include <cstdint>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {

    struct TextureImportSettings {
        TextureUsage usage = TextureUsage::Auto;
        TextureAssetDimension dimension = TextureAssetDimension::Texture2D;
        TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
        TextureCompression compression = TextureCompression::Auto;
        TextureMipPolicy mipPolicy = TextureMipPolicy::Auto;
        CookedAssetFormat outputFormat = CookedAssetFormat::DDS;

        bool forcePowerOfTwo = false;
        bool allowResize = false;
        uint32_t maxSize = 4096;

        bool sourceHasAlphaChannel = false;
        bool sourceHasMeaningfulAlpha = false;
        bool sourceHasTranslucentAlpha = false;
        bool sourceHasCutoutAlpha = false;
        float sourceAlphaNonOpaqueRatio = 0.0f;
        float sourceAlphaTranslucentRatio = 0.0f;
        float sourceAlphaCutoutRatio = 0.0f;
    };

} // namespace HIKARI
