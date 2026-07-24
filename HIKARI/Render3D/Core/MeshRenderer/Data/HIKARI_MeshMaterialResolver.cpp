#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshMaterialResolver.h"

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {
    namespace {
        const TextureAsset3D* FindTextureByIndex(const ModelAsset& asset, int textureIndex) {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(asset.textures.size())) {
                return nullptr;
            }
            return &asset.textures[static_cast<size_t>(textureIndex)];
        }

        const std::string& SelectRuntimeTexturePath(const TextureAsset3D& texture) {
            return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;

            // Asset pipeline 済みなら cooked path、未解決なら元画像へフォールバックする。
            return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
        }

        int BackendOrFallback(RENDER3D::TextureResourceHandle resource, int fallbackHandle) {
            const int handle = RENDER3D::GetTextureResourceBackendHandle(resource);
            return handle >= 0 ? handle : fallbackHandle;
        }

        RENDER3D::TextureResourceHandle LoadMaterialTextureResource(
            const std::string& debugNamePrefix,
            const std::string& texturePath,
            RENDER3D::TextureResourceColorSpace colorSpace) {

            return RENDER3D::LoadTextureResourceWithColorSpace(
                debugNamePrefix + texturePath,
                texturePath,
                colorSpace);
        }

        bool TryResolveLoadedMaterialTexture(
            std::unordered_map<std::string, RENDER3D::TextureResourceHandle>& cache,
            const std::string& cacheKey,
            const std::string& debugNamePrefix,
            const std::string& texturePath,
            RENDER3D::TextureResourceColorSpace colorSpace,
            int fallbackHandle,
            int& outBackendHandle) {

            const RENDER3D::TextureResourceHandle resource =
                RENDER3D::FindLoadedTextureResourceWithColorSpace(
                    debugNamePrefix + texturePath,
                    texturePath,
                    colorSpace);
            if (!resource) {
                return false;
            }

            cache[cacheKey] = resource;
            outBackendHandle = BackendOrFallback(resource, fallbackHandle);
            return true;
        }
    }

    void MeshMaterialResolver::SetFallbacks(const MeshMaterialResolverFallbacks& fallbacks) {
        fallbacks_ = fallbacks;
    }

    void MeshMaterialResolver::BeginFrame(size_t maxTextureLoads) {
        textureLoadBudgetRemaining_ = maxTextureLoads;
    }

    void MeshMaterialResolver::ClearCache() {
        for (const auto& entry : materialTextureCache_) {
            RENDER3D::ReleaseTextureResource(entry.second);
        }
        materialTextureCache_.clear();
    }

    bool MeshMaterialResolver::TryAcquireTextureLoadBudget(MeshRendererDebugStats* stats) {
        if (textureLoadBudgetRemaining_ == 0) {
            resolveDeferred_ = true;
            if (stats != nullptr) {
                ++stats->materialTextureLoadDeferredCount;
            }
            return false;
        }

        --textureLoadBudgetRemaining_;
        return true;
    }

    ResolvedMaterialTextures MeshMaterialResolver::Resolve(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        resolveDeferred_ = false;
        ResolvedMaterialTextures textures{};
        textures.baseColor = ResolveBaseColorTexture(asset, materialAsset, stats);
        textures.normal = ResolveNormalTexture(asset, materialAsset, stats);
        textures.emissive = ResolveEmissiveTexture(asset, materialAsset, stats);
        textures.metallicRoughness = ResolveMetallicRoughnessTexture(asset, materialAsset, stats);
        textures.occlusion = ResolveOcclusionTexture(asset, materialAsset, stats);
        textures.specular = ResolveSpecularTexture(asset, materialAsset, stats);
        textures.specularColor = ResolveSpecularColorTexture(asset, materialAsset, stats);
        textures.complete = !resolveDeferred_;
        return textures;
    }

    int MeshMaterialResolver::GetFallbackWhite() const {
        return fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::GetFallbackNormal() const {
        return fallbacks_.normalTexture;
    }

    int MeshMaterialResolver::GetFallbackBlack() const {
        return fallbacks_.blackTexture;
    }

    int MeshMaterialResolver::ResolveBaseColorTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->baseColorTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            return fallbacks_.whiteTexture;
        }

        auto found = materialTextureCache_.find(texturePath);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.whiteTexture);
        }

        int loadedHandle = fallbacks_.whiteTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                texturePath,
                "model_material/baseColor/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Srgb,
                fallbacks_.whiteTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.whiteTexture;
        }
        if (stats != nullptr) {
            ++stats->materialTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/baseColor/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Srgb);
        materialTextureCache_[texturePath] = resource;
        return BackendOrFallback(resource, fallbacks_.whiteTexture);
    }

    int MeshMaterialResolver::ResolveNormalTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.normalTexture;
        }

        const int textureIndex = materialAsset->normalTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            if (stats != nullptr) {
                ++stats->normalMapFallbackCount;
            }
            return fallbacks_.normalTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->normalMapFallbackCount;
            }
            return fallbacks_.normalTexture;
        }

        const std::string cacheKey = "normal:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->normalTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.normalTexture);
        }

        int loadedHandle = fallbacks_.normalTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/normal/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Linear,
                fallbacks_.normalTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->normalTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.normalTexture;
        }
        if (stats != nullptr) {
            ++stats->normalTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/normal/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Linear);
        materialTextureCache_[cacheKey] = resource;
        return BackendOrFallback(resource, fallbacks_.normalTexture);
    }

    int MeshMaterialResolver::ResolveEmissiveTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.blackTexture;
        }

        const int textureIndex = materialAsset->emissiveTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            if (stats != nullptr) {
                ++stats->emissiveMapFallbackCount;
            }
            return fallbacks_.blackTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->emissiveMapFallbackCount;
            }
            return fallbacks_.blackTexture;
        }

        const std::string cacheKey = "emissive:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->emissiveTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.blackTexture);
        }

        int loadedHandle = fallbacks_.blackTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/emissive/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Srgb,
                fallbacks_.blackTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->emissiveTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.blackTexture;
        }
        if (stats != nullptr) {
            ++stats->emissiveTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/emissive/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Srgb);
        materialTextureCache_[cacheKey] = resource;
        return BackendOrFallback(resource, fallbacks_.blackTexture);
    }

    int MeshMaterialResolver::ResolveMetallicRoughnessTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->metallicRoughnessTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "metallicRoughness:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.whiteTexture);
        }

        int loadedHandle = fallbacks_.whiteTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/metallic_roughness/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Linear,
                fallbacks_.whiteTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->metallicRoughnessTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.whiteTexture;
        }
        if (stats != nullptr) {
            ++stats->metallicRoughnessTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/metallic_roughness/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Linear);
        materialTextureCache_[cacheKey] = resource;
        const int handle = RENDER3D::GetTextureResourceBackendHandle(resource);
        if (handle < 0) {
            DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] metallicRoughness texture failed. material=") +
                materialAsset->name + " sourcePath=" + texturePath + " fallback used");
        }
        return handle >= 0 ? handle : fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::ResolveOcclusionTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->occlusionTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            if (stats != nullptr) {
                ++stats->occlusionFallbackCount;
            }
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "occlusion:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->occlusionTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.whiteTexture);
        }

        int loadedHandle = fallbacks_.whiteTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/occlusion/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Linear,
                fallbacks_.whiteTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->occlusionTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.whiteTexture;
        }
        if (stats != nullptr) {
            ++stats->occlusionTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/occlusion/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Linear);
        materialTextureCache_[cacheKey] = resource;
        const int handle = RENDER3D::GetTextureResourceBackendHandle(resource);
        if (handle < 0) {
            DEBUGLOG::PushRenderError(std::string("[MeshRenderer][PBRTexture][WARN] occlusion texture failed. material=") +
                materialAsset->name + " sourcePath=" + texturePath + " fallback used");
        }
        return handle >= 0 ? handle : fallbacks_.whiteTexture;
    }

    int MeshMaterialResolver::ResolveSpecularTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->specularTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "specular:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.whiteTexture);
        }

        int loadedHandle = fallbacks_.whiteTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/specular/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Linear,
                fallbacks_.whiteTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.whiteTexture;
        }
        if (stats != nullptr) {
            ++stats->materialTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/specular/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Linear);
        materialTextureCache_[cacheKey] = resource;
        return BackendOrFallback(resource, fallbacks_.whiteTexture);
    }

    int MeshMaterialResolver::ResolveSpecularColorTexture(
        const ModelAsset& asset,
        const MaterialAsset* materialAsset,
        MeshRendererDebugStats* stats) {
        if (materialAsset == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const int textureIndex = materialAsset->specularColorTexture.textureIndex;
        const TextureAsset3D* texture = FindTextureByIndex(asset, textureIndex);
        if (texture == nullptr) {
            return fallbacks_.whiteTexture;
        }

        const std::string& texturePath = SelectRuntimeTexturePath(*texture);
        if (texturePath.empty()) {
            return fallbacks_.whiteTexture;
        }

        const std::string cacheKey = "specularColor:" + texturePath;
        auto found = materialTextureCache_.find(cacheKey);
        if (found != materialTextureCache_.end()) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return BackendOrFallback(found->second, fallbacks_.whiteTexture);
        }

        int loadedHandle = fallbacks_.whiteTexture;
        if (TryResolveLoadedMaterialTexture(
                materialTextureCache_,
                cacheKey,
                "model_material/specular_color/",
                texturePath,
                RENDER3D::TextureResourceColorSpace::Srgb,
                fallbacks_.whiteTexture,
                loadedHandle)) {
            if (stats != nullptr) {
                ++stats->materialTextureCacheHitCount;
            }
            return loadedHandle;
        }

        if (!TryAcquireTextureLoadBudget(stats)) {
            return fallbacks_.whiteTexture;
        }
        if (stats != nullptr) {
            ++stats->materialTextureCacheMissCount;
        }
        const RENDER3D::TextureResourceHandle resource = LoadMaterialTextureResource(
            "model_material/specular_color/",
            texturePath,
            RENDER3D::TextureResourceColorSpace::Srgb);
        materialTextureCache_[cacheKey] = resource;
        return BackendOrFallback(resource, fallbacks_.whiteTexture);
    }

} // namespace HIKARI::MESHRENDERER
