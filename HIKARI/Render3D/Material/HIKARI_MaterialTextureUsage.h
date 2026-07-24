#pragma once

#include <cstdint>

namespace HIKARI {

    namespace RENDER3D {
        enum class TextureResourceColorSpace : uint8_t;
    }

    enum class MaterialTextureUsage {
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        Emissive,
        Specular,
        SpecularColor,
        Unknown,
    };

    const char* MaterialTextureUsageName(MaterialTextureUsage usage);

    RENDER3D::TextureResourceColorSpace MaterialTextureColorSpace(
        MaterialTextureUsage usage);

} // namespace HIKARI
