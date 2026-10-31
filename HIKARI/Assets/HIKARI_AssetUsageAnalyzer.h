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

// 使用中と分かった資産の中身を読めず、その先の依存を辿れなかったもの。
// 削除保護・書き出し・焼き込みの依存ハッシュは、この間は使用状況を確かめられないものとして扱う。
struct AssetUnreadableReference {
    std::string assetId{};
    std::string owner{};
    std::string reason{};
};

struct AssetUsageSummary {
    std::unordered_set<std::string> usedGuids{};
    std::vector<AssetMissingReference> missingReferences{};
    std::vector<AssetUnreadableReference> unreadableReferences{};

    bool IsUsed(const AssetGuid& guid) const { return guid.IsValid() && usedGuids.find(guid.value) != usedGuids.end(); }
};

enum class AssetUsageScope : uint8_t {
    Scene,
    ObjectComponents,
};

AssetUsageSummary AnalyzeAssetUsage(const SceneDocument& document, const AssetDatabase& assetDatabase,
                                    AssetUsageScope scope = AssetUsageScope::Scene);

} // namespace HIKARI
