#include "Editor/Assets/Browser/HIKARI_AssetBrowserCache.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Project/HIKARI_ProjectSettings.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

const AssetGuid& AssetBrowserProjectSettingsCache::StartupScene(const AssetDatabase& database) {
    const auto now = std::chrono::steady_clock::now();
    const auto& root = database.GetProjectRoot();
    const bool rootChanged = root != projectRoot_;
    if (!rootChanged && now < nextCheck_) return startupScene_;
    nextCheck_ = now + std::chrono::milliseconds(500);
    if (rootChanged) {
        startupScene_ = {};
        projectRoot_ = root;
        loaded_ = false;
    }
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(
        root / "ProjectSettings" / "project_settings.json", error);
    if (error) return startupScene_;
    if (!loaded_ || modified != modified_) {
        ProjectSettingsService settings;
        if (!settings.Load(root)) return startupScene_;
        startupScene_ = settings.GetSettings().startupSceneGuid;
        modified_ = modified;
        loaded_ = true;
    }
    return startupScene_;
}

void AssetBrowserStatisticsCache::Update(const AssetDatabase& database) {
    const auto now = std::chrono::steady_clock::now();
    if (database_ == &database && projectRoot_ == database.GetProjectRoot() &&
        revision_ == database.GetContentRevision() && now < nextCheck_) return;
    database_ = &database;
    projectRoot_ = database.GetProjectRoot();
    revision_ = database.GetContentRevision();
    nextCheck_ = now + std::chrono::milliseconds(500);
    counts_.clear();
    total_ = broken_ = 0;
    for (const auto* record : database.CollectAll()) {
        if (!record) continue;
        ++total_;
        ++counts_[record->type];
        if (IsBrokenAssetRecord(*record)) ++broken_;
    }
}

int AssetBrowserStatisticsCache::Count(AssetType type) const noexcept {
    const auto found = counts_.find(type);
    return found == counts_.end() ? 0 : found->second;
}

namespace {
size_t UsageDocumentFingerprint(const SceneDocument& document) {
    size_t fingerprint = document.objects.size();
    const auto append = [&](size_t value) {
        fingerprint ^= value + 0x9e3779b9u + (fingerprint << 6u) + (fingerprint >> 2u);
    };
    const std::hash<std::string> stringHash;
    const std::hash<nlohmann::json> propertyHash;
    append(stringHash(document.environment.sky.skyAsset));
    append(stringHash(document.environment.sky.panoramaCloudAsset));
    append(stringHash(document.environment.reflectionProbe.sourceCubemapAsset));
    for (const auto& object : document.objects) {
        append(static_cast<size_t>(object.id.value));
        append(stringHash(object.name));
        for (const auto& component : object.components) {
            append(stringHash(component.type));
            append(propertyHash(component.properties));
        }
    }
    return fingerprint;
}
}

bool AssetBrowserUsageCache::Update(const SceneDocument& document,
    uint64_t documentRevision, const AssetDatabase& database, uint64_t editRevision) {
    const uint64_t databaseRevision = database.GetContentRevision();
    const bool sameIdentity = document_ == &document && objects_ == document.objects.data() &&
        database_ == &database && projectRoot_ == database.GetProjectRoot() &&
        documentRevision_ == documentRevision && databaseRevision_ == databaseRevision;
    const auto now = std::chrono::steady_clock::now();
    if (editRevision != 0 && sameIdentity && editRevision_ == editRevision && now < nextDocumentScan_)
        return false;
    editRevision_ = editRevision;
    nextDocumentScan_ = now + std::chrono::milliseconds(250);
    const size_t fingerprint = UsageDocumentFingerprint(document);
    if (document_ == &document && objects_ == document.objects.data() &&
        database_ == &database && projectRoot_ == database.GetProjectRoot() &&
        documentRevision_ == documentRevision && documentFingerprint_ == fingerprint &&
        databaseRevision_ == databaseRevision) {
        return false;
    }
    summary_ = AnalyzeAssetUsage(document, database);
    document_ = &document;
    objects_ = document.objects.data();
    database_ = &database;
    projectRoot_ = database.GetProjectRoot();
    documentRevision_ = documentRevision;
    documentFingerprint_ = fingerprint;
    databaseRevision_ = databaseRevision;
    ++generation_;
    return true;
}

bool AssetBrowserCatalogCache::Update(const AssetDatabase& database,
    const AssetBrowserQuery& query, const AssetUsageSummary* usage,
    uint64_t usageRevision) {
    const bool databaseChanged = database_ != &database ||
        projectRoot_ != database.GetProjectRoot() ||
        databaseRevision_ != database.GetContentRevision();
    const bool usesUsage = query.scope == AssetBrowserScope::CurrentScene ||
        query.scope == AssetBrowserScope::UnusedInScene;
    const auto now = std::chrono::steady_clock::now();
    const bool refreshState = query.stateFilter != 0 && now >= nextStateCheck_;
    if (!databaseChanged && !refreshState && query_ == query &&
        (!usesUsage || (usage_ == usage && usageRevision_ == usageRevision &&
            (usage == nullptr || usageRevision != 0u)))) {
        return false;
    }
    // ポインターを再利用する前に、資産データベースの世代を検証する。
    if (databaseChanged) directories_ = database.CollectDirectories();
    auto records = query.scope == AssetBrowserScope::Project
        ? database.CollectInDirectory(query.directory, query.recursive)
        : database.CollectAll();
    std::vector<std::pair<std::string, const AssetRecord*>> sorted;
    sorted.reserve(records.size());
    for (const AssetRecord* record : records) {
        if (record && MatchesScope(*record, usage, query.scope) &&
            MatchesTypeFilter(*record, query.typeFilter) &&
            MatchesStateFilter(*record, query.stateFilter) &&
            MatchesSearch(*record, query.search.c_str())) {
            sorted.emplace_back(TEXT::ToLowerAsciiCopy(record->displayName), record);
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    records_.clear();
    records_.reserve(sorted.size());
    for (const auto& entry : sorted) records_.push_back(entry.second);
    database_ = &database;
    projectRoot_ = database.GetProjectRoot();
    databaseRevision_ = database.GetContentRevision();
    query_ = query;
    nextStateCheck_ = now + std::chrono::milliseconds(500);
    usage_ = usage;
    usageRevision_ = usageRevision;
    return true;
}

}
