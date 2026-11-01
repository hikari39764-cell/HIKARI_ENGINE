#include "Render3D/Models/Runtime/HIKARI_ModelTextureResolver.h"

#include <algorithm>
#include <utility>

#include "RenderContracts/Model/HIKARI_ModelAssetLookup.h"
#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"
#include "Core/HIKARI_Logger.h"

namespace HIKARI::RENDER3D::MODELS {

void ModelTextureResolver::SetPathResolver(ModelTexturePathResolver resolver) { pathResolver_ = std::move(resolver); }

void ModelTextureResolver::ResetStats() noexcept { stats_ = {}; }

void ModelTextureResolver::RecordFailure(ModelTextureResolveFailureKind kind) const noexcept {
    switch (kind) {
    case ModelTextureResolveFailureKind::Missing: ++stats_.missing; break;
    case ModelTextureResolveFailureKind::Ambiguous: ++stats_.ambiguous; break;
    default: break;
    }
}

const ModelTextureResolveStats& ModelTextureResolver::GetStats() const noexcept { return stats_; }

std::string ModelTextureResolver::ResolvePath(const std::string& sourceTexturePath, MaterialTextureUsage usage) const {
    if (sourceTexturePath.empty()) {
        return {};
    }

    ++stats_.total;

    if (!pathResolver_) {
        ++stats_.fallbackRaw;
        HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " reason=resolver not configured");
        return sourceTexturePath;
    }

    const std::string resolvedPath = pathResolver_(sourceTexturePath, usage);
    if (resolvedPath.empty()) {
        ++stats_.fallbackRaw;
        HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " reason=resolver returned empty");
        return sourceTexturePath;
    }

    if (resolvedPath == sourceTexturePath) {
        if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(resolvedPath) == CookedAssetFormat::HTEX) {
            ++stats_.resolvedHtex;
            HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " + sourceTexturePath +
                            " usage=" + MaterialTextureUsageName(usage));
        } else {
            ++stats_.fallbackRaw;
            HIKARI_LOG_INFO("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                            " usage=" + MaterialTextureUsageName(usage));
        }
    } else if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(resolvedPath) == CookedAssetFormat::HTEX) {
        ++stats_.resolvedHtex;
        HIKARI_LOG_INFO("[ModelTextureResolver] resolved HTEX: " + sourceTexturePath + " -> " + resolvedPath +
                        " usage=" + MaterialTextureUsageName(usage));
    } else {
        ++stats_.fallbackRaw;
        HIKARI_LOG_INFO("[ModelTextureResolver] source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " resolved=" + resolvedPath);
    }

    return resolvedPath;
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

    // 構造化描画は ModelAsset の texture 配列から SRV を引くため、ここで cooked パスへ寄せる。
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
