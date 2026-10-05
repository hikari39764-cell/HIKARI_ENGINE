#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "Assets/HIKARI_AssetArtifactManifest.h"

namespace HIKARI {
class AssetDatabase;
class AssetImportPublication;
struct AssetRecord;
struct AssetMeta;

AssetImportInputFile CaptureAssetInputFile(const std::filesystem::path& path);
std::string CanonicalImportSettings(std::string_view settings);
bool ValidateAssetImportSettings(const AssetMeta& edited, const AssetMeta& defaults, std::string& error);
bool EquivalentAssetImportMeta(const AssetMeta& left, const AssetMeta& right);
AssetImportInputs CaptureAssetImportInputs(const AssetDatabase& database, const AssetRecord& record,
                                           const std::vector<AssetDependencyDesc>& dependencies,
                                           const AssetImportPublication* publication = nullptr);
bool CheckAssetImportInputs(const std::filesystem::path& projectRoot, const AssetImportInputs& inputs,
                            std::string& reason, const AssetImportPublication* publication = nullptr,
                            std::unordered_map<std::string, std::string>* scanDigests = nullptr);
} // namespace HIKARI
