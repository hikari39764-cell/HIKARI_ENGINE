#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"

#include <algorithm>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::MATERIAL {
    namespace {

        constexpr size_t ToIndex(
            GpuMaterialTextureChannel channel) noexcept {
            return static_cast<size_t>(channel);
        }

        uint32_t ResolveDescriptorIndex(
            const RuntimeTextureSlot& texture) noexcept {
            if (!texture.IsActive()) {
                return kInvalidTextureDescriptorIndex;
            }
            const UINT descriptor = texture.resource.IsValid()
                ? GetTextureResourceSrvDescriptorIndex(texture.resource)
                : GetTextureResourceSrvDescriptorIndexFromBackendHandle(
                    texture.ResolveBackendHandle());
            return descriptor == UINT32_MAX
                ? kInvalidTextureDescriptorIndex
                : static_cast<uint32_t>(descriptor);
        }

        GpuMaterialTextureProjection ProjectTexture(
            const RuntimeTextureSlot& texture) noexcept {
            GpuMaterialTextureProjection result{};
            result.backendHandle = texture.ResolveBackendHandle();
            result.descriptorIndex = ResolveDescriptorIndex(texture);
            result.uvTransform = {
                texture.uvScale.x,
                texture.uvScale.y,
                texture.uvOffset.x,
                texture.uvOffset.y
            };
            result.uvRotation = texture.uvRotation;
            result.uvSet = static_cast<uint32_t>(
                std::clamp(texture.texCoord, 0, 1));
            result.enabled = texture.IsActive() &&
                result.descriptorIndex != kInvalidTextureDescriptorIndex;
            return result;
        }

        GpuMaterialTextureProjection& Texture(
            GpuMaterialProjection& projection,
            GpuMaterialTextureChannel channel) noexcept {
            return projection.textures[ToIndex(channel)];
        }

        const GpuMaterialTextureProjection& Texture(
            const GpuMaterialProjection& projection,
            GpuMaterialTextureChannel channel) noexcept {
            return projection.textures[ToIndex(channel)];
        }

    } // namespace

    GpuMaterialProjection BuildRuntimeMaterialGpuProjection(
        const Material& material) {
        GpuMaterialProjection result{};
        result.baseColor = material.GetBaseColor();
        const MATH::Vec3 emissive = material.GetEmissiveFactor();
        result.emissiveFactor = {
            emissive.x,
            emissive.y,
            emissive.z,
            material.GetEmissiveStrength()
        };
        result.pbrParams = {
            material.GetMetallicFactor(),
            material.GetRoughnessFactor(),
            material.GetOcclusionStrength(),
            material.GetAlphaCutoff()
        };
        const MATH::Vec3 specular = material.GetSpecularColorFactor();
        result.specularParams = {
            specular.x,
            specular.y,
            specular.z,
            material.GetSpecularFactor()
        };
        result.normalScale = material.GetNormalScale();

        Texture(result, GpuMaterialTextureChannel::BaseColor) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::BaseColor));
        Texture(result, GpuMaterialTextureChannel::Normal) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::Normal));
        Texture(result, GpuMaterialTextureChannel::Emissive) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::Emissive));
        Texture(result, GpuMaterialTextureChannel::MetallicRoughness) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::MetallicRoughness));
        Texture(result, GpuMaterialTextureChannel::Occlusion) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::Occlusion));
        Texture(result, GpuMaterialTextureChannel::Specular) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::Specular));
        Texture(result, GpuMaterialTextureChannel::SpecularColor) =
            ProjectTexture(material.GetTextureSlot(
                MaterialTextureUsage::SpecularColor));
        return result;
    }

    void ProjectMaterialTextureUv(
        const MaterialAsset* modelMaterial,
        const Material* runtimeMaterial,
        MaterialTextureUsage usage,
        GpuMaterialTextureProjection& outTexture) {
        const auto apply = [&outTexture](const auto& slot) {
            outTexture.uvTransform = {
                slot.uvScale.x, slot.uvScale.y, slot.uvOffset.x, slot.uvOffset.y};
            outTexture.uvRotation = slot.uvRotation;
            outTexture.uvSet = static_cast<uint32_t>(std::clamp(slot.texCoord, 0, 1));
        };
        if (runtimeMaterial != nullptr) {
            const RuntimeTextureSlot& slot = runtimeMaterial->GetTextureSlot(usage);
            if (!runtimeMaterial->InheritsModelTextures() || slot.enabled) {
                apply(slot);
                return;
            }
        }
        outTexture.uvTransform = {1.0f, 1.0f, 0.0f, 0.0f};
        outTexture.uvRotation = 0.0f;
        outTexture.uvSet = 0u;
        if (modelMaterial == nullptr) return;
        switch (usage) {
        case MaterialTextureUsage::BaseColor: apply(modelMaterial->baseColorTexture); break;
        case MaterialTextureUsage::Normal: apply(modelMaterial->normalTexture); break;
        case MaterialTextureUsage::Emissive: apply(modelMaterial->emissiveTexture); break;
        case MaterialTextureUsage::MetallicRoughness: apply(modelMaterial->metallicRoughnessTexture); break;
        case MaterialTextureUsage::Occlusion: apply(modelMaterial->occlusionTexture); break;
        case MaterialTextureUsage::Specular: apply(modelMaterial->specularTexture); break;
        case MaterialTextureUsage::SpecularColor: apply(modelMaterial->specularColorTexture); break;
        default: break;
        }
    }

    GpuMaterialData PackGpuMaterialData(
        const GpuMaterialProjection& projection) {
        GpuMaterialData data{};
        data.baseColor = projection.baseColor;
        data.emissiveFactor = projection.emissiveFactor;
        data.pbrParams = projection.pbrParams;
        data.specularParams = projection.specularParams;
        data.materialFlags = projection.materialFlags;
        data.normalScale = projection.normalScale;

        const auto apply = [](const GpuMaterialTextureProjection& texture,
                              uint32_t& enabled,
                              int32_t& handle,
                              uint32_t& descriptor,
                              MATH::Vec4& uvTransform) {
            enabled = texture.enabled ? 1u : 0u;
            handle = texture.backendHandle;
            descriptor = texture.descriptorIndex;
            uvTransform = texture.uvTransform;
        };
        apply(Texture(projection, GpuMaterialTextureChannel::BaseColor),
            data.hasBaseColorTexture, data.baseColorTextureHandle,
            data.baseColorTextureDescriptorIndex, data.baseColorUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::Normal),
            data.hasNormalTexture, data.normalTextureHandle,
            data.normalTextureDescriptorIndex, data.normalUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::Emissive),
            data.hasEmissiveTexture, data.emissiveTextureHandle,
            data.emissiveTextureDescriptorIndex, data.emissiveUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::MetallicRoughness),
            data.hasMetallicRoughnessTexture,
            data.metallicRoughnessTextureHandle,
            data.metallicRoughnessTextureDescriptorIndex,
            data.metallicRoughnessUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::Occlusion),
            data.hasOcclusionTexture, data.occlusionTextureHandle,
            data.occlusionTextureDescriptorIndex, data.occlusionUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::Specular),
            data.hasSpecularTexture, data.specularTextureHandle,
            data.specularTextureDescriptorIndex, data.specularUvTransform);
        apply(Texture(projection, GpuMaterialTextureChannel::SpecularColor),
            data.hasSpecularColorTexture, data.specularColorTextureHandle,
            data.specularColorTextureDescriptorIndex,
            data.specularColorUvTransform);

        const auto& base = Texture(
            projection, GpuMaterialTextureChannel::BaseColor);
        const auto& normal = Texture(
            projection, GpuMaterialTextureChannel::Normal);
        const auto& emissive = Texture(
            projection, GpuMaterialTextureChannel::Emissive);
        const auto& metallicRoughness = Texture(
            projection, GpuMaterialTextureChannel::MetallicRoughness);
        const auto& occlusion = Texture(
            projection, GpuMaterialTextureChannel::Occlusion);
        const auto& specular = Texture(
            projection, GpuMaterialTextureChannel::Specular);
        const auto& specularColor = Texture(
            projection, GpuMaterialTextureChannel::SpecularColor);
        data.uvRotation0 = {
            base.uvRotation, normal.uvRotation,
            emissive.uvRotation, metallicRoughness.uvRotation
        };
        data.uvRotation1 = {
            occlusion.uvRotation, specular.uvRotation,
            specularColor.uvRotation, 0.0f
        };
        data.uvSet0[0] = base.uvSet;
        data.uvSet0[1] = normal.uvSet;
        data.uvSet0[2] = emissive.uvSet;
        data.uvSet0[3] = metallicRoughness.uvSet;
        data.uvSet1[0] = occlusion.uvSet;
        data.uvSet1[1] = specular.uvSet;
        data.uvSet1[2] = specularColor.uvSet;
        return data;
    }

    std::vector<uint32_t>
        ResolveMaterialProgramResourceDescriptorIndices(
            const Material& material) {
        const std::span<const RuntimeTextureSlot> resources =
            material.GetMaterialProgramResourceBlock();
        std::vector<uint32_t> descriptors(
            resources.size(), kInvalidTextureDescriptorIndex);
        for (size_t index = 0u; index < resources.size(); ++index) {
            descriptors[index] = ResolveDescriptorIndex(resources[index]);
        }
        return descriptors;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
