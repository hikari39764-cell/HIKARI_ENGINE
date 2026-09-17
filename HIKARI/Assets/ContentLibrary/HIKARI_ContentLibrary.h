#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::ASSETS::LIBRARY {
    // ライブラリは制作時の交換形式。実行時は既存のプロジェクト資産と Cook を使う。
    struct ContentPackageInfo {
        std::string id;
        std::string version = "1";
        std::string name;
        std::string category;
        std::string description;
        std::string license;
        std::filesystem::path manifestPath;
        std::vector<AssetGuid> roots;
        size_t assetCount = 0;
    };

    struct ContentImportResult {
        std::vector<AssetGuid> roots;
        std::vector<AssetGuid> assets;
        bool reused = false;
    };

    bool ReadContentPackage(const std::filesystem::path& manifestPath,
        ContentPackageInfo& outInfo, std::string& outMessage);
    std::vector<ContentPackageInfo> ScanContentLibrary(
        const std::filesystem::path& root, std::string& outMessage);
    bool PublishContentPackage(const AssetDatabase& database,
        std::span<const AssetGuid> roots, const std::filesystem::path& libraryRoot,
        const ContentPackageInfo& info, std::string& outMessage);
    bool ImportContentPackage(AssetDatabase& database,
        const std::filesystem::path& manifestPath,
        ContentImportResult& outResult, std::string& outMessage);
}
