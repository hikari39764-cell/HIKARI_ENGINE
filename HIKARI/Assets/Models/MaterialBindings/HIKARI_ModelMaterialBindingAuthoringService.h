#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::ASSETS::MODELS {

struct ModelMaterialBindingAuthoringDocument {
    AssetGuid assetGuid{};
    std::filesystem::path sourcePath{};
    ModelMaterialBindingAssetData data{};
    std::vector<std::string> migrationDiagnostics{};
};

struct ModelMaterialBindingAuthoringResult {
    bool success = false;
    bool sourceSaved = false;
    bool assetScanned = false;
    bool imported = false;
    AssetGuid assetGuid{};
    std::filesystem::path sourcePath{};
    std::vector<std::string> validationDiagnostics{};
    std::string message{};
};

class ModelMaterialBindingAuthoringService {
public:
    explicit ModelMaterialBindingAuthoringService(AssetDatabase& assetDatabase) noexcept;

    bool Load(const AssetGuid& assetGuid, ModelMaterialBindingAuthoringDocument& outDocument,
              std::string& outMessage) const;

    ModelMaterialBindingAuthoringResult Create(const AssetGuid& modelAssetGuid, std::string_view displayName,
                                               const std::filesystem::path& directory = {});

    ModelMaterialBindingAuthoringResult Update(const AssetGuid& assetGuid, const ModelMaterialBindingAssetData& data);

    ModelMaterialBindingAuthoringResult Save(const AssetGuid& assetGuid, const ModelMaterialBindingAssetData& data);

    ModelMaterialBindingAuthoringResult SaveAs(const ModelMaterialBindingAssetData& data,
                                               const std::filesystem::path& destination);

    ModelMaterialBindingAuthoringResult ImportAndScan(const std::filesystem::path& sourcePath,
                                                      bool createMissingMeta = true);

private:
    ModelMaterialBindingAuthoringResult SaveToPath(const ModelMaterialBindingAssetData& data,
                                                   const std::filesystem::path& destination, bool createMissingMeta);

    AssetDatabase& assetDatabase_;
};

} // namespace HIKARI::ASSETS::MODELS
