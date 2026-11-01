#pragma once

#include <span>

#include "RenderContracts/Material/HIKARI_MaterialProgramFormat.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"

namespace HIKARI::RENDER3D::MATERIAL {

void ApplyConnectedMaterialResponseParameters(const MaterialProgramIR& program, std::span<MATH::Vec4> parameters,
                                              std::span<const MaterialProgramParameterValueOverride> overrides = {});

} // namespace HIKARI::RENDER3D::MATERIAL
