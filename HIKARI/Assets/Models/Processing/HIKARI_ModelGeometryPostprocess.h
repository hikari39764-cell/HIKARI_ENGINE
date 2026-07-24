#pragma once

#include <cstdint>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS {

    bool ModelMaterialHasNormalTexture(
        const ModelAsset& asset,
        uint32_t materialIndex) noexcept;

    void GenerateModelPrimitiveNormals(MeshPrimitive& primitive);
    void GenerateModelPrimitiveTangents(MeshPrimitive& primitive);

} // namespace HIKARI::ASSETS::MODELS
