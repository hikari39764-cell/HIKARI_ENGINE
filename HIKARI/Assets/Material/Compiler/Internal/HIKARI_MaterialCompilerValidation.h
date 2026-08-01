#pragma once

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {

    bool ValidateMaterialSource(
        const MaterialAssetData& source,
        MaterialCompileResult& result);

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
