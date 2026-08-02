#pragma once

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {

void BuildMaterialProgramInstructions(
    const MaterialGraphData& graph,
    MaterialProgramIR& program);

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
