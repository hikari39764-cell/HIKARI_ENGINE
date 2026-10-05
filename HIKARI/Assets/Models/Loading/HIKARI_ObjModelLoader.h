#pragma once

#include "Assets/HIKARI_AssetImportInputs.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS {

bool LoadObjModelSource(ModelAsset& asset, std::vector<AssetImportInputFile>* sourceInputs = nullptr);

} // namespace HIKARI::ASSETS::MODELS
