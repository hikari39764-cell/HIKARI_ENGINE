#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <json.hpp>
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::REFERENCE {
struct ReferenceModel {
    std::string name;
    AssetGuid guid;
    Bounds bounds;
};

inline void Require(bool success, const std::string& message) {
    if (!success)
        throw std::runtime_error(message);
}

AssetGuid ReferenceGuid(const std::filesystem::path& relativePath);
void WriteReferenceMeta(AssetDatabase& database, const std::filesystem::path& source);
nlohmann::json CreateValidationScenes(const std::filesystem::path& root, AssetDatabase& database,
                                      const std::vector<ReferenceModel>& models);
void CreateValidationProject(const std::filesystem::path& destination, const std::filesystem::path& assetPack);
void VerifyValidationProject(const std::filesystem::path& root);
} // namespace HIKARI::REFERENCE
