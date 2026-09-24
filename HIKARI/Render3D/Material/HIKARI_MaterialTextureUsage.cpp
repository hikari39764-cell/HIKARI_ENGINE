#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"

#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/Core/HIKARI_Material.h"

namespace HIKARI {

const char* MaterialTextureUsageName(MaterialTextureUsage usage) {
    switch (usage) {
    case MaterialTextureUsage::BaseColor: return "baseColor";
    case MaterialTextureUsage::Normal: return "normal";
    case MaterialTextureUsage::MetallicRoughness: return "metallicRoughness";
    case MaterialTextureUsage::Occlusion: return "occlusion";
    case MaterialTextureUsage::Emissive: return "emissive";
    case MaterialTextureUsage::Specular: return "specular";
    case MaterialTextureUsage::SpecularColor: return "specularColor";
    default: return "unknown";
    }
}

RENDER3D::TextureResourceColorSpace MaterialTextureColorSpace(MaterialTextureUsage usage) {
    switch (usage) {
    case MaterialTextureUsage::BaseColor:
    case MaterialTextureUsage::Emissive:
    case MaterialTextureUsage::SpecularColor: return RENDER3D::TextureResourceColorSpace::Srgb;
    case MaterialTextureUsage::Normal:
    case MaterialTextureUsage::MetallicRoughness:
    case MaterialTextureUsage::Occlusion:
    case MaterialTextureUsage::Specular:
    default: return RENDER3D::TextureResourceColorSpace::Linear;
    }
}

const TextureSlot* GetModelMaterialTextureSlot(const MaterialAsset* material, MaterialTextureUsage usage) {
    if (material == nullptr)
        return nullptr;
    switch (usage) {
    case MaterialTextureUsage::BaseColor: return &material->baseColorTexture;
    case MaterialTextureUsage::Normal: return &material->normalTexture;
    case MaterialTextureUsage::MetallicRoughness: return &material->metallicRoughnessTexture;
    case MaterialTextureUsage::Occlusion: return &material->occlusionTexture;
    case MaterialTextureUsage::Emissive: return &material->emissiveTexture;
    case MaterialTextureUsage::Specular: return &material->specularTexture;
    case MaterialTextureUsage::SpecularColor: return &material->specularColorTexture;
    default: return nullptr;
    }
}

const RuntimeTextureSlot* ResolveMaterialTextureOverride(const Material* material, MaterialTextureUsage usage) {
    if (material == nullptr || usage == MaterialTextureUsage::Unknown)
        return nullptr;
    const auto& slot = material->GetTextureSlot(usage);
    return !material->InheritsModelTextures() || slot.enabled ? &slot : nullptr;
}

const std::string& GetMaterialTextureRuntimePath(const TextureAsset3D& texture) {
    return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
}

const std::string& GetMaterialTextureRuntimePath(const RuntimeTextureSlot& texture) {
    return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
}

} // namespace HIKARI
