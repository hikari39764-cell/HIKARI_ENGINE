#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"

#include <algorithm>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "RenderContracts/Model/Policies/HIKARI_ModelMaterialPolicy.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::MATERIAL {
namespace {

constexpr size_t ToIndex(GpuMaterialTextureChannel channel) noexcept { return static_cast<size_t>(channel); }

uint32_t ResolveDescriptorIndex(const RuntimeTextureSlot& texture) noexcept {
    if (!texture.IsActive()) {
        return kInvalidTextureDescriptorIndex;
    }
    const UINT descriptor = texture.resource.IsValid()
                                ? GetTextureResourceSrvDescriptorIndex(texture.resource)
                                : GetTextureResourceSrvDescriptorIndexFromBackendHandle(texture.ResolveBackendHandle());
    return descriptor == UINT32_MAX ? kInvalidTextureDescriptorIndex : static_cast<uint32_t>(descriptor);
}

GpuMaterialTextureProjection ProjectTexture(const RuntimeTextureSlot& texture) noexcept {
    GpuMaterialTextureProjection result{};
    result.backendHandle = texture.ResolveBackendHandle();
    result.descriptorIndex = ResolveDescriptorIndex(texture);
    result.uvTransform = {texture.uvScale.x, texture.uvScale.y, texture.uvOffset.x, texture.uvOffset.y};
    result.uvRotation = texture.uvRotation;
    result.uvSet = static_cast<uint32_t>(std::clamp(texture.texCoord, 0, 1));
    result.enabled = texture.IsActive() && result.descriptorIndex != kInvalidTextureDescriptorIndex;
    return result;
}

GpuMaterialTextureProjection& Texture(GpuMaterialProjection& projection, GpuMaterialTextureChannel channel) noexcept {
    return projection.textures[ToIndex(channel)];
}

const GpuMaterialTextureProjection& Texture(const GpuMaterialProjection& projection,
                                            GpuMaterialTextureChannel channel) noexcept {
    return projection.textures[ToIndex(channel)];
}

} // namespace

AlphaMode ResolveMaterialAlphaMode(const MaterialAsset* modelMaterial, const Material* runtimeMaterial) {
    const uint32_t flags = runtimeMaterial != nullptr ? runtimeMaterial->GetFeatureBits()
                           : modelMaterial != nullptr ? modelMaterial->featureBits
                                                      : 0u;
    if (runtimeMaterial != nullptr) {
        switch (runtimeMaterial->GetMaterialAlphaMode()) {
        case RuntimeMaterialAlphaMode::Mask: return AlphaMode::Mask;
        case RuntimeMaterialAlphaMode::Blend: return AlphaMode::Blend;
        default: break;
        }
    } else if (modelMaterial != nullptr && modelMaterial->alphaMode != AlphaMode::Opaque) {
        return modelMaterial->alphaMode;
    }
    // 旧 API の feature bits を保持する。Opaque の色 alpha から透明度を推測しない。
    if ((flags & MATERIAL_FEATURES::AlphaMask) != 0u)
        return AlphaMode::Mask;
    if ((flags & MATERIAL_FEATURES::ThinTransparentSurface) != 0u)
        return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

MaterialFactorProjection BuildMaterialFactorProjection(const MaterialAsset* modelMaterial,
                                                       const Material* runtimeMaterial) {
    MaterialFactorProjection result{};
    if (modelMaterial != nullptr) {
        const auto& material = *modelMaterial;
        result.baseColor = material.baseColorFactor;
        result.emissiveFactor = {material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z,
                                 material.emissiveStrength};
        result.pbrParams = {material.metallicFactor, material.roughnessFactor, material.occlusionTexture.strength,
                            material.alphaCutoff};
        result.specularParams = {material.specularColorFactor.x, material.specularColorFactor.y,
                                 material.specularColorFactor.z, material.specularFactor};
        result.normalScale = material.normalTexture.scale;
        result.materialFlags = material.featureBits;
    }
    if (runtimeMaterial != nullptr) {
        const auto& material = *runtimeMaterial;
        result.baseColor = material.GetBaseColor();
        const MATH::Vec3 emissive = material.GetEmissiveFactor();
        result.emissiveFactor = {emissive.x, emissive.y, emissive.z, material.GetEmissiveStrength()};
        result.pbrParams = {material.GetMetallicFactor(), material.GetRoughnessFactor(),
                            material.GetOcclusionStrength(), material.GetAlphaCutoff()};
        const MATH::Vec3 specular = material.GetSpecularColorFactor();
        result.specularParams = {specular.x, specular.y, specular.z, material.GetSpecularFactor()};
        result.normalScale = material.GetNormalScale();
        result.materialFlags = material.GetFeatureBits();
        const MaterialShellSettings& shell = material.GetShellSettings();
        result.shellStroke0 = {shell.strokeTiling, shell.strokeRotation, shell.strokeVariation,
                               shell.strokeNormalStrength};
        result.shellStroke1 = {shell.edgePower, shell.coverage, static_cast<float>(shell.maxLayers), shell.thickness};
        result.surfaceLayers = material.GetSurfaceLayers();
        result.subsurface = {material.GetSubsurface().x, material.GetSubsurface().y, 0.0f, 0.0f};
    }
    result.materialFlags &= ~(MATERIAL_FEATURES::AlphaMask | MATERIAL_FEATURES::ThinTransparentSurface);
    const AlphaMode alpha = ResolveMaterialAlphaMode(modelMaterial, runtimeMaterial);
    if (alpha == AlphaMode::Mask)
        result.materialFlags |= MATERIAL_FEATURES::AlphaMask;
    if (alpha == AlphaMode::Blend)
        result.materialFlags |= MATERIAL_FEATURES::ThinTransparentSurface;
    return result;
}

GpuMaterialProjection BuildRuntimeMaterialGpuProjection(const Material& material) {
    GpuMaterialProjection result{BuildMaterialFactorProjection(nullptr, &material)};
    for (size_t i = 0; i < kGpuMaterialTextureUsages.size(); ++i)
        result.textures[i] = ProjectTexture(material.GetTextureSlot(kGpuMaterialTextureUsages[i]));
    return result;
}

GpuMaterialTextureProjection ProjectRuntimeMaterialTexture(const Material& material, MaterialTextureUsage usage) {
    return ProjectTexture(material.GetTextureSlot(usage));
}

void ProjectMaterialTextureUv(const MaterialAsset* modelMaterial, const Material* runtimeMaterial,
                              MaterialTextureUsage usage, GpuMaterialTextureProjection& outTexture) {
    const auto apply = [&outTexture](const auto& slot) {
        outTexture.uvTransform = {slot.uvScale.x, slot.uvScale.y, slot.uvOffset.x, slot.uvOffset.y};
        outTexture.uvRotation = slot.uvRotation;
        outTexture.uvSet = static_cast<uint32_t>(std::clamp(slot.texCoord, 0, 1));
    };
    if (const auto* slot = ResolveMaterialTextureOverride(runtimeMaterial, usage)) {
        apply(*slot);
        return;
    }
    outTexture.uvTransform = {1.0f, 1.0f, 0.0f, 0.0f};
    outTexture.uvRotation = 0.0f;
    outTexture.uvSet = 0u;
    if (const auto* slot = GetModelMaterialTextureSlot(modelMaterial, usage))
        apply(*slot);
}

GpuMaterialData PackGpuMaterialData(const GpuMaterialProjection& projection) {
    GpuMaterialData data{};
    data.baseColor = projection.baseColor;
    data.emissiveFactor = projection.emissiveFactor;
    data.pbrParams = projection.pbrParams;
    data.specularParams = projection.specularParams;
    data.materialFlags = projection.materialFlags;
    data.normalScale = projection.normalScale;

    const auto apply = [](const GpuMaterialTextureProjection& texture, uint32_t& enabled, int32_t& handle,
                          uint32_t& descriptor, MATH::Vec4& uvTransform) {
        enabled = texture.enabled ? 1u : 0u;
        handle = texture.backendHandle;
        descriptor = texture.descriptorIndex;
        uvTransform = texture.uvTransform;
    };
    apply(Texture(projection, GpuMaterialTextureChannel::BaseColor), data.hasBaseColorTexture,
          data.baseColorTextureHandle, data.baseColorTextureDescriptorIndex, data.baseColorUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::Normal), data.hasNormalTexture, data.normalTextureHandle,
          data.normalTextureDescriptorIndex, data.normalUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::Emissive), data.hasEmissiveTexture, data.emissiveTextureHandle,
          data.emissiveTextureDescriptorIndex, data.emissiveUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::MetallicRoughness), data.hasMetallicRoughnessTexture,
          data.metallicRoughnessTextureHandle, data.metallicRoughnessTextureDescriptorIndex,
          data.metallicRoughnessUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::Occlusion), data.hasOcclusionTexture,
          data.occlusionTextureHandle, data.occlusionTextureDescriptorIndex, data.occlusionUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::Specular), data.hasSpecularTexture, data.specularTextureHandle,
          data.specularTextureDescriptorIndex, data.specularUvTransform);
    apply(Texture(projection, GpuMaterialTextureChannel::SpecularColor), data.hasSpecularColorTexture,
          data.specularColorTextureHandle, data.specularColorTextureDescriptorIndex, data.specularColorUvTransform);

    const auto& base = Texture(projection, GpuMaterialTextureChannel::BaseColor);
    const auto& normal = Texture(projection, GpuMaterialTextureChannel::Normal);
    const auto& emissive = Texture(projection, GpuMaterialTextureChannel::Emissive);
    const auto& metallicRoughness = Texture(projection, GpuMaterialTextureChannel::MetallicRoughness);
    const auto& occlusion = Texture(projection, GpuMaterialTextureChannel::Occlusion);
    const auto& specular = Texture(projection, GpuMaterialTextureChannel::Specular);
    const auto& specularColor = Texture(projection, GpuMaterialTextureChannel::SpecularColor);
    data.uvRotation0 = {base.uvRotation, normal.uvRotation, emissive.uvRotation, metallicRoughness.uvRotation};
    data.uvRotation1 = {occlusion.uvRotation, specular.uvRotation, specularColor.uvRotation, 0.0f};
    data.uvSet0[0] = base.uvSet;
    data.uvSet0[1] = normal.uvSet;
    data.uvSet0[2] = emissive.uvSet;
    data.uvSet0[3] = metallicRoughness.uvSet;
    data.uvSet1[0] = occlusion.uvSet;
    data.uvSet1[1] = specular.uvSet;
    data.uvSet1[2] = specularColor.uvSet;
    const auto& strokeMask = Texture(projection, GpuMaterialTextureChannel::StrokeMask);
    const auto& strokeNormal = Texture(projection, GpuMaterialTextureChannel::StrokeNormal);
    data.strokeMaskTextureDescriptorIndex =
        strokeMask.enabled ? strokeMask.descriptorIndex : kInvalidTextureDescriptorIndex;
    data.strokeNormalTextureDescriptorIndex =
        strokeNormal.enabled ? strokeNormal.descriptorIndex : kInvalidTextureDescriptorIndex;
    data.shellStroke0 = projection.shellStroke0;
    data.shellStroke1 = projection.shellStroke1;
    data.surfaceLayers = projection.surfaceLayers;
    data.subsurface = projection.subsurface;
    return data;
}

std::vector<uint32_t> ResolveMaterialProgramResourceDescriptorIndices(const Material& material) {
    const std::span<const RuntimeTextureSlot> resources = material.GetMaterialProgramResourceBlock();
    std::vector<uint32_t> descriptors(resources.size(), kInvalidTextureDescriptorIndex);
    for (size_t index = 0u; index < resources.size(); ++index) {
        descriptors[index] = ResolveDescriptorIndex(resources[index]);
    }
    return descriptors;
}

} // namespace HIKARI::RENDER3D::MATERIAL
