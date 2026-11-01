#include "Assets/Material/HIKARI_MaterialAssetLookup.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI::ASSETS::MATERIAL {

bool LoadMaterialAssetSource(const AssetDatabase& assetDatabase, const AssetGuid& assetGuid,
                             MaterialAssetData& outSource, std::string& outError) {
    const AssetRecord* record = assetDatabase.FindByGuid(assetGuid);
    if (record == nullptr || record->type != AssetType::Material) {
        outError = "Material asset record was not found";
        return false;
    }
    const std::filesystem::path sourcePath = assetDatabase.ResolveSourcePath(*record);
    return MATERIAL_SERIALIZATION::LoadMaterialSourceJson(sourcePath, outSource, outError);
}

MaterialCompileOptions MakeMaterialCompileOptions(const AssetDatabase& assetDatabase) {
    MaterialCompileOptions options{};
    options.resolveAsset = [&assetDatabase](const AssetGuid& assetGuid, MaterialAssetData& outSource,
                                            std::string& outError) {
        return LoadMaterialAssetSource(assetDatabase, assetGuid, outSource, outError);
    };
    return options;
}

} // namespace HIKARI::ASSETS::MATERIAL
