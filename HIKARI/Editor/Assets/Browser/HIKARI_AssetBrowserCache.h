#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {
class AssetDatabase;
struct AssetRecord;
struct SceneDocument;
enum class AssetBrowserScope;
} // namespace HIKARI

namespace HIKARI::EDITOR::ASSET_BROWSER {

class AssetBrowserProjectSettingsCache {
public:
    const AssetGuid& StartupScene(const AssetDatabase& database);

private:
    std::filesystem::path projectRoot_{};
    std::filesystem::file_time_type modified_{};
    std::chrono::steady_clock::time_point nextCheck_{};
    AssetGuid startupScene_{};
    bool loaded_ = false;
};

class AssetBrowserStatisticsCache {
public:
    void Update(const AssetDatabase& database);
    int Total() const noexcept { return total_; }
    int Broken() const noexcept { return broken_; }
    int Count(AssetType type) const noexcept;

private:
    const AssetDatabase* database_ = nullptr;
    std::filesystem::path projectRoot_{};
    uint64_t revision_ = 0;
    std::chrono::steady_clock::time_point nextCheck_{};
    int total_ = 0;
    int broken_ = 0;
    std::unordered_map<AssetType, int> counts_{};
};

class AssetBrowserUsageCache {
public:
    bool Update(const SceneDocument& document, uint64_t documentRevision, const AssetDatabase& database,
                uint64_t editRevision = 0);
    const AssetUsageSummary& Summary() const noexcept { return summary_; }
    uint64_t Generation() const noexcept { return generation_; }

private:
    const SceneDocument* document_ = nullptr;
    const void* objects_ = nullptr;
    const AssetDatabase* database_ = nullptr;
    std::filesystem::path projectRoot_{};
    uint64_t documentRevision_ = 0;
    size_t documentFingerprint_ = 0;
    uint64_t editRevision_ = 0;
    std::chrono::steady_clock::time_point nextDocumentScan_{};
    uint64_t databaseRevision_ = 0;
    uint64_t generation_ = 0;
    AssetUsageSummary summary_{};
};

struct AssetBrowserQuery {
    std::filesystem::path directory{"Assets"};
    std::string search{};
    AssetBrowserScope scope{};
    int typeFilter = 0;
    int stateFilter = 0;
    bool recursive = false;
    bool operator==(const AssetBrowserQuery&) const = default;
};

class AssetBrowserCatalogCache {
public:
    bool Update(const AssetDatabase& database, const AssetBrowserQuery& query, const AssetUsageSummary* usage,
                uint64_t usageRevision);
    const std::vector<const AssetRecord*>& Records() const noexcept { return records_; }
    const std::vector<std::filesystem::path>& Directories() const noexcept { return directories_; }

private:
    const AssetDatabase* database_ = nullptr;
    std::filesystem::path projectRoot_{};
    uint64_t databaseRevision_ = 0;
    const AssetUsageSummary* usage_ = nullptr;
    uint64_t usageRevision_ = 0;
    AssetBrowserQuery query_{};
    std::chrono::steady_clock::time_point nextStateCheck_{};
    std::vector<const AssetRecord*> records_{};
    std::vector<std::filesystem::path> directories_{};
};

} // namespace HIKARI::EDITOR::ASSET_BROWSER
