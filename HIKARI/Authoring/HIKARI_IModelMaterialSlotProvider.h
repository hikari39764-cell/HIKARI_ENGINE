#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"

namespace HIKARI {

class AssetDatabase;

struct ModelMaterialSlotAuthoringView {
    uint32_t slotIndex = 0u;
    MaterialSlotId slotId{};
    std::string name{};
    bool hasBaseColorTexture = false;
};

struct ModelMaterialSetAuthoringView {
    AssetGuid assetGuid{};
    std::string name{};
};

// Scene component は HMODEL の I/O や Editor cache を所有しない。
// Inspector はこの狭い読み取り専用境界から import 済み slot を取得する。
class IModelMaterialSlotProvider {
public:
    virtual ~IModelMaterialSlotProvider() = default;

    virtual std::span<const ModelMaterialSlotAuthoringView> ResolveMaterialSlots(const AssetDatabase& database,
                                                                                 const AssetGuid& modelGuid,
                                                                                 std::string& outDiagnostic) = 0;

    virtual std::span<const ModelMaterialSetAuthoringView> ResolveCompatibleMaterialSets(
        const AssetDatabase& database, const AssetGuid& modelGuid, std::string& outDiagnostic) = 0;
};

} // namespace HIKARI
