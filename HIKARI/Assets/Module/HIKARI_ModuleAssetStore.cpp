#include "Assets/Module/HIKARI_ModuleAssetStore.h"

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Project/Paths/HIKARI_ProjectDataFiles.h"

namespace HIKARI {

bool LoadModuleAssetText(const std::filesystem::path& path, ModuleAssetText& out, std::string& error) {
    uint64_t byteCount = 0u;
    if (PROJECT_DATA::InspectProjectDataTextFile(path, byteCount, error) !=
            PROJECT_DATA::ProjectDataFileCode::Resolved ||
        PROJECT_DATA::ReadProjectDataTextFile(path, byteCount, out.text, error) !=
            PROJECT_DATA::ProjectDataFileCode::Resolved) {
        return false;
    }
    out.revision = HASH::Fnv1aString(out.text) | 1u;
    if (const ASSETS::SEMANTICS::ModuleAssetTypeDesc* type = ASSETS::SEMANTICS::FindModuleAssetTypeForPath(path)) {
        out.typeId = type->typeId;
    }
    return true;
}

std::shared_ptr<const ModuleAssetText> ModuleAssetStore::Load(const AssetGuid& guid, std::string_view moduleAssetTypeId,
                                                              std::string& error) const {
    // 作ったばかりでまだ導入を通っていないファイルも読む（中身を確かめるのは読む側のモジュール）。
    std::shared_ptr<const ModuleAssetText> asset = cache_.Load(guid, {}, error, true);
    if (asset != nullptr && asset->typeId != moduleAssetTypeId) {
        error = "Asset " + guid.value + " is not a " + std::string(moduleAssetTypeId) + " asset.";
        return {};
    }
    return asset;
}

} // namespace HIKARI
