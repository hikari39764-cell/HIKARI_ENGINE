#pragma once
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Assets/HIKARI_AssetRecord.h"
#include <unordered_map>
#include <unordered_set>

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::ASSETS::COOKING::INTERNAL {
struct CookPlan {
    std::unordered_set<std::string> assetGuids{};
    std::unordered_set<std::string> sceneGuids{};
    std::unordered_map<std::string, std::vector<AssetDependencyDesc>> sourceDependencies{};
    size_t missingReferenceCount = 0u;
};

std::string MakePathKey(const std::filesystem::path& path);
bool IsPathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root);
bool IsDeclaredProjectAssetRoot(const std::filesystem::path& root);
bool ResolveContainedFilePath(const std::filesystem::path& root, const std::filesystem::path& relativePath,
                              std::filesystem::path& outResolved, std::string& outMessage);
bool CopyCookPayloadFile(const std::filesystem::path& source, const std::filesystem::path& destination,
                         size_t& fileCount, std::string& outMessage);
bool BuildCookPlan(const AssetCookOptions& options, AssetDatabase& assetDatabase, CookPlan& outPlan,
                   std::string& outMessage);
const std::vector<AssetDependencyDesc>& CookDependencies(const AssetRecord& record, const CookPlan& plan);
bool CopyOpeningAnimation(const std::filesystem::path& projectRoot, const std::filesystem::path& contentRoot,
                          size_t& fileCount, std::string& outMessage);
bool CopyRuntimeSettingsAndData(const AssetCookOptions& options, size_t& fileCount, std::string& outMessage);
bool CopyDeclaredRuntimePayloads(const AssetCookOptions& options, size_t& fileCount, std::string& outMessage);
bool CopyLightingContent(const AssetCookOptions& options, const CookPlan& plan, size_t& fileCount,
                         std::string& outMessage);
bool CookProceduralGeometry(const AssetCookOptions& options, const CookPlan& plan, const AssetDatabase& assetDatabase,
                            size_t& fileCount, std::string& outMessage);
} // namespace HIKARI::ASSETS::COOKING::INTERNAL
