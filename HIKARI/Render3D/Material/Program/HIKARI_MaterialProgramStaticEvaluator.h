#pragma once

#include <span>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI::RENDER3D::MATERIAL {

void ApplyConnectedMaterialResponseParameters(
    const MaterialProgramIR& program,
    std::span<MATH::Vec4> parameters);

} // namespace HIKARI::RENDER3D::MATERIAL
