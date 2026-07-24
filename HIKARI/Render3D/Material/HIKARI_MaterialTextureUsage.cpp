#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"

#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {

    const char* MaterialTextureUsageName(MaterialTextureUsage usage) {
        switch (usage) {
        case MaterialTextureUsage::BaseColor:
            return "baseColor";
        case MaterialTextureUsage::Normal:
            return "normal";
        case MaterialTextureUsage::MetallicRoughness:
            return "metallicRoughness";
        case MaterialTextureUsage::Occlusion:
            return "occlusion";
        case MaterialTextureUsage::Emissive:
            return "emissive";
        case MaterialTextureUsage::Specular:
            return "specular";
        case MaterialTextureUsage::SpecularColor:
            return "specularColor";
        default:
            return "unknown";
        }
    }

    RENDER3D::TextureResourceColorSpace MaterialTextureColorSpace(
        MaterialTextureUsage usage) {

        switch (usage) {
        case MaterialTextureUsage::BaseColor:
        case MaterialTextureUsage::Emissive:
        case MaterialTextureUsage::SpecularColor:
            return RENDER3D::TextureResourceColorSpace::Srgb;
        case MaterialTextureUsage::Normal:
        case MaterialTextureUsage::MetallicRoughness:
        case MaterialTextureUsage::Occlusion:
        case MaterialTextureUsage::Specular:
        default:
            return RENDER3D::TextureResourceColorSpace::Linear;
        }
    }

} // namespace HIKARI
