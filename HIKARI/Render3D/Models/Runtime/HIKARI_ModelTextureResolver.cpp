#include "Render3D/Models/Runtime/HIKARI_ModelTextureResolver.h"

#include <algorithm>
#include <utility>

#include "Assets/Models/HIKARI_ModelAssetLookup.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::MODELS {

    void ModelTextureResolver::SetPathResolver(ModelTexturePathResolver resolver) {
        pathResolver_ = std::move(resolver);
    }

    void ModelTextureResolver::ClearPathResolver() {
        pathResolver_ = {};
    }

    void ModelTextureResolver::ResetStats() noexcept {
        stats_ = {};
    }

    void ModelTextureResolver::RecordFailure(ModelTextureResolveFailureKind kind) const noexcept {
        switch (kind) {
        case ModelTextureResolveFailureKind::Missing:
            ++stats_.missing;
            break;
        case ModelTextureResolveFailureKind::Ambiguous:
            ++stats_.ambiguous;
            break;
        default:
            break;
        }
    }

    const ModelTextureResolveStats& ModelTextureResolver::GetStats() const noexcept {
        return stats_;
    }

    std::string ModelTextureResolver::ResolvePath(
        const std::string& sourceTexturePath,
        MaterialTextureUsage usage) const {

        if (sourceTexturePath.empty()) {
            return {};
        }

        ++stats_.total;

        if (!pathResolver_) {
            ++stats_.fallbackRaw;
            HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " reason=resolver not configured");
            return sourceTexturePath;
        }

        const std::string resolvedPath = pathResolver_(sourceTexturePath, usage);
        if (resolvedPath.empty()) {
            ++stats_.fallbackRaw;
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " reason=resolver returned empty");
            return sourceTexturePath;
        }

        if (resolvedPath == sourceTexturePath) {
            if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(resolvedPath) ==
                CookedAssetFormat::HTEX) {
                ++stats_.resolvedHtex;
                HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " +
                    sourceTexturePath +
                    " usage=" + MaterialTextureUsageName(usage));
            } else {
                ++stats_.fallbackRaw;
                HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" +
                    sourceTexturePath +
                    " usage=" + MaterialTextureUsageName(usage));
            }
        } else if (
            ASSETS::SEMANTICS::ClassifyCookedAssetFormat(resolvedPath) ==
            CookedAssetFormat::HTEX) {
            ++stats_.resolvedHtex;
            HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " +
                sourceTexturePath +
                " -> " + resolvedPath +
                " usage=" + MaterialTextureUsageName(usage));
        } else {
            ++stats_.fallbackRaw;
            HIKARI_LOG_INFO("[ModelTextureResolver] source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " resolved=" + resolvedPath);
        }

        return resolvedPath;
    }

    RuntimeTextureSlot ModelTextureResolver::LoadMaterialTexture(
        const ModelAsset& asset,
        const std::string& textureName,
        const TextureSlot& textureSlot,
        MaterialTextureUsage usage) const {

        RuntimeTextureSlot slot{};
        const TextureAsset3D* texture = ASSETS::MODELS::FindModelTexture(asset, textureSlot);
        if (texture == nullptr || texture->sourcePath.empty()) {
            return slot;
        }

        slot.sourcePath = texture->sourcePath;
        slot.resolvedPath = texture->resolvedPath.empty()
            ? ResolvePath(texture->sourcePath, usage)
            : texture->resolvedPath;
        if (slot.resolvedPath.empty()) {
            slot.resolvedPath = slot.sourcePath;
        }
        slot.texCoord = std::clamp(textureSlot.texCoord, 0, 1);
        slot.uvScale = textureSlot.uvScale;
        slot.uvOffset = textureSlot.uvOffset;
        slot.uvRotation = textureSlot.uvRotation;

        slot.resource = RENDER3D::LoadTextureResourceWithColorSpace(
            textureName,
            slot.resolvedPath,
            MaterialTextureColorSpace(usage));
        slot.handle = RENDER3D::GetTextureResourceBackendHandle(slot.resource);
        slot.enabled = slot.handle >= 0;
        return slot;
    }

    void ModelTextureResolver::ApplyMaterial(
        const ModelAsset& asset,
        const MaterialAsset& source,
        Material& runtimeMaterial,
        const std::string& materialNamePrefix) const {

        runtimeMaterial.SetBaseColor(source.baseColorFactor);
        runtimeMaterial.SetMetallicFactor(source.metallicFactor);
        runtimeMaterial.SetRoughnessFactor(source.roughnessFactor);
        runtimeMaterial.SetSpecularFactor(source.specularFactor);
        runtimeMaterial.SetSpecularColorFactor(source.specularColorFactor);
        runtimeMaterial.SetNormalScale(source.normalTexture.scale);
        runtimeMaterial.SetOcclusionStrength(source.occlusionTexture.strength);
        runtimeMaterial.SetEmissiveFactor(source.emissiveFactor);
        runtimeMaterial.SetEmissiveStrength(source.emissiveStrength);
        runtimeMaterial.SetFeatureBits(source.featureBits);
        runtimeMaterial.SetShaderProfileId(source.shaderProfileId);

        // 螳溯｡梧凾 Material 縺ｯ縲∝・繝代せ縺ｨ隗｣豎ｺ貂医∩ cooked 繝代せ繧剃ｸ｡譁ｹ菫晄戟縺吶ｋ縲・
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::BaseColor, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_baseColor",
            source.baseColorTexture,
            MaterialTextureUsage::BaseColor));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::Normal, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_normal",
            source.normalTexture,
            MaterialTextureUsage::Normal));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::MetallicRoughness, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_metallicRoughness",
            source.metallicRoughnessTexture,
            MaterialTextureUsage::MetallicRoughness));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::Occlusion, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_occlusion",
            source.occlusionTexture,
            MaterialTextureUsage::Occlusion));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::Emissive, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_emissive",
            source.emissiveTexture,
            MaterialTextureUsage::Emissive));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::Specular, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_specular",
            source.specularTexture,
            MaterialTextureUsage::Specular));
        runtimeMaterial.SetTextureSlot(MaterialTextureUsage::SpecularColor, LoadMaterialTexture(
            asset,
            materialNamePrefix + "_specularColor",
            source.specularColorTexture,
            MaterialTextureUsage::SpecularColor));
    }

    void ModelTextureResolver::ResolveAssetTexturePaths(ModelAsset& asset) const {
        auto resolveSlot = [this, &asset](const TextureSlot& slot, MaterialTextureUsage usage) {
            TextureAsset3D* texture = ASSETS::MODELS::FindModelTexture(asset, slot);
            if (texture == nullptr || texture->sourcePath.empty()) {
                return;
            }
            if (texture->resolvedPath.empty()) {
                texture->resolvedPath = ResolvePath(texture->sourcePath, usage);
            }
            if (texture->resolvedPath.empty()) {
                texture->resolvedPath = texture->sourcePath;
            }
        };

        // 讒矩蛹匁緒逕ｻ縺ｯ ModelAsset 縺ｮ texture 驟榊・縺九ｉ SRV 繧貞ｼ輔￥縺溘ａ縲√％縺薙〒 cooked 繝代せ縺ｸ蟇・○繧九・
        for (const MaterialAsset& material : asset.materials) {
            resolveSlot(material.baseColorTexture, MaterialTextureUsage::BaseColor);
            resolveSlot(material.normalTexture, MaterialTextureUsage::Normal);
            resolveSlot(material.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness);
            resolveSlot(material.occlusionTexture, MaterialTextureUsage::Occlusion);
            resolveSlot(material.emissiveTexture, MaterialTextureUsage::Emissive);
            resolveSlot(material.specularTexture, MaterialTextureUsage::Specular);
            resolveSlot(material.specularColorTexture, MaterialTextureUsage::SpecularColor);
        }
    }


} // namespace HIKARI::RENDER3D::MODELS
