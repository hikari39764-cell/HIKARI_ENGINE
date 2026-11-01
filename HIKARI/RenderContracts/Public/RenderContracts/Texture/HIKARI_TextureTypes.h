#pragma once

#include <cstdint>

namespace HIKARI {

enum class TextureAssetDimension {
    Texture2D,
    TextureCube,
};

enum class TextureAssetColorSpace {
    Auto,
    Linear,
    Srgb,
};

enum class TextureUsage {
    Auto,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Mask,
    UI,
    SkyCubemap,
    IblIrradiance,
    IblPrefiltered,
    BrdfLut,
};

enum class TextureCompression {
    Auto,
    None,
    BC1,
    BC3,
    BC4,
    BC5,
    BC6H,
    BC7,
};

enum class TextureMipPolicy {
    Auto,
    Generate,
    Preserve,
    None,
};

enum class TextureAssetAlphaClass : uint8_t {
    Unknown,
    Opaque,
    Cutout,
    Translucent,
};

} // namespace HIKARI
