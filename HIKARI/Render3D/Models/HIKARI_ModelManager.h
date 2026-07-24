#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Models/Runtime/HIKARI_ModelTextureResolver.h"

namespace HIKARI {

    class ModelManager {
    public:
        ModelAsset* RegisterAsset(
            const std::string& name,
            const std::string& sourcePath);
        ModelAsset* FindAsset(const std::string& name);
        const ModelAsset* FindAsset(const std::string& name) const;

        const std::vector<std::unique_ptr<ModelAsset>>& GetAssets() const;

        bool MarkLoaded(const std::string& name);
        bool MarkFailed(const std::string& name);

        bool LoadAssetNow(const std::string& name);
        bool ReloadAssetNow(const std::string& name);
        void UnloadAsset(const std::string& name);
        void UnloadAllAssets();
        bool LoadAllRegisteredAssets();

        size_t CountLoadedAssets() const;
        size_t CountFailedAssets() const;

        void SetTexturePathResolver(ModelTexturePathResolver resolver);
        void ClearTexturePathResolver();
        void ResetTextureResolveStats();
        void RecordTextureResolveFailure(
            ModelTextureResolveFailureKind kind) const;
        const ModelTextureResolveStats& GetTextureResolveStats() const;

    private:
        std::vector<std::unique_ptr<ModelAsset>> assets_;
        std::unordered_map<std::string, ModelAsset*> nameToAsset_;
        RENDER3D::MODELS::ModelTextureResolver textureResolver_{};
    };

} // namespace HIKARI
