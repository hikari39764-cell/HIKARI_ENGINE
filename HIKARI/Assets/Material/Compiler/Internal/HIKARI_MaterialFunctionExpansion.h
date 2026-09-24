#pragma once

#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {

bool ExpandMaterialFunctions(const MaterialAssetData& source, const MaterialCompileOptions& options,
                             MaterialAssetData& outExpanded, std::vector<AssetGuid>& outFunctionDependencies,
                             MaterialCompileResult& result);

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
