#pragma once

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL {

    void AddSkinLightingGraph(MaterialAssetData& data);
    void AddFabricSheenGraph(MaterialAssetData& data);
    void AddHybridLightingGraph(MaterialAssetData& data);

} // namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL
