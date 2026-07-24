#pragma once

#include <functional>
#include <string>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"

namespace HIKARI {

    using ModelTexturePathResolver = std::function<std::string(
        const std::string& sourceTexturePath,
        MaterialTextureUsage usage)>;

    enum class ModelTextureResolveFailureKind {
        Missing,
        Ambiguous,
    };

    struct ModelTextureResolveStats {
        int total = 0;
        int resolvedHtex = 0;
        int fallbackRaw = 0;
        int missing = 0;
        int ambiguous = 0;
    };

} // namespace HIKARI

namespace HIKARI::RENDER3D::MODELS {

    class ModelTextureResolver {
    public:
        void SetPathResolver(ModelTexturePathResolver resolver);
        void ClearPathResolver();

        void ResetStats() noexcept;
        void RecordFailure(ModelTextureResolveFailureKind kind) const noexcept;
        const ModelTextureResolveStats& GetStats() const noexcept;

        std::string ResolvePath(
            const std::string& sourceTexturePath,
            MaterialTextureUsage usage) const;

        void ResolveAssetTexturePaths(ModelAsset& asset) const;

        RuntimeTextureSlot LoadMaterialTexture(
            const ModelAsset& asset,
            const std::string& textureName,
            const TextureSlot& textureSlot,
            MaterialTextureUsage usage) const;

        void ApplyMaterial(
            const ModelAsset& asset,
            const MaterialAsset& source,
            Material& runtimeMaterial,
            const std::string& materialNamePrefix) const;

    private:
        ModelTexturePathResolver pathResolver_{};
        mutable ModelTextureResolveStats stats_{};
    };

} // namespace HIKARI::RENDER3D::MODELS
