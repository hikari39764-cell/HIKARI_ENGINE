#pragma once

#include <cstdint>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS {

void GenerateModelPrimitiveNormals(MeshPrimitive& primitive);
void GenerateModelPrimitiveTangents(MeshPrimitive& primitive);

} // namespace HIKARI::ASSETS::MODELS
