#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "RenderContracts/Material/HIKARI_MaterialProgramFormat.h"

namespace HIKARI {

using MaterialCompileAssetResolver =
    std::function<bool(const AssetGuid& assetGuid, MaterialAssetData& outSource, std::string& outError)>;

struct MaterialCompileOptions {
    MaterialCompileAssetResolver resolveAsset{};
    uint32_t maximumFunctionDepth = 16u;
};

MaterialProgramIR BuildStandardPbrMaterialProgramFallback();

MaterialCompileResult CompileMaterialAsset(const MaterialAssetData& source);
MaterialCompileResult CompileMaterialAsset(const MaterialAssetData& source, const MaterialCompileOptions& options);

} // namespace HIKARI
