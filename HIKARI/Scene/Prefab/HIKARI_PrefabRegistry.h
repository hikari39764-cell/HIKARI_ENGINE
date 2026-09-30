#pragma once

#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

class AssetDatabase;
class PrefabSerializer;
struct PrefabDocument;

struct PrefabAssetEntry {
    AssetGuid guid{};
    std::string name{};
};

class PrefabRegistry {
public:
    bool Save(AssetDatabase& assetDatabase, const std::string& name, const PrefabDocument& document,
              const PrefabSerializer& serializer, AssetGuid& outGuid, std::string& outError) const;
    bool Load(const AssetDatabase& assetDatabase, const AssetGuid& guid, PrefabDocument& outDocument,
              const PrefabSerializer& serializer, std::string& outError) const;
    std::vector<PrefabAssetEntry> List(const AssetDatabase& assetDatabase) const;
};

} // namespace HIKARI
