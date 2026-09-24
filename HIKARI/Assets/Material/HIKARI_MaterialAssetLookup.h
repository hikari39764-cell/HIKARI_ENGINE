#pragma once

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::ASSETS::MATERIAL {

bool LoadMaterialAssetSource(const AssetDatabase& assetDatabase, const AssetGuid& assetGuid,
                             MaterialAssetData& outSource, std::string& outError);

MaterialCompileOptions MakeMaterialCompileOptions(const AssetDatabase& assetDatabase);

} // namespace HIKARI::ASSETS::MATERIAL
