#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "HIKARI_AssetGuid.h"

namespace HIKARI {

class AssetDatabase;
struct SceneDocument;

struct AssetMissingReference {
    std::string assetId{};
    std::string owner{};
    std::string role{};
};

struct AssetUsageSummary {
    std::unordered_set<std::string> usedGuids{};
    std::vector<AssetMissingReference> missingReferences{};

    bool IsUsed(const AssetGuid& guid) const { return guid.IsValid() && usedGuids.find(guid.value) != usedGuids.end(); }
};

enum class AssetUsageScope : uint8_t {
    Scene,
    ObjectComponents,
};

AssetUsageSummary AnalyzeAssetUsage(const SceneDocument& document, const AssetDatabase& assetDatabase,
                                    AssetUsageScope scope = AssetUsageScope::Scene);

} // namespace HIKARI
