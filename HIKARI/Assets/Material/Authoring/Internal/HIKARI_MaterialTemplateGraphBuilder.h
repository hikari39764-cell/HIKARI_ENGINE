#pragma once

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL {

void AddNode(MaterialAssetData&, std::string id, std::string displayName, MaterialNodeKind, MaterialValueType, float x,
             float y, std::string referenceId = {});
void ConnectNodeInput(MaterialAssetData&, std::string id, std::string sourceNode, std::string sourcePin,
                      std::string targetNode, std::string targetPin);

void AddSkinLightingGraph(MaterialAssetData& data);
void AddFabricSheenGraph(MaterialAssetData& data);
void AddHybridLightingGraph(MaterialAssetData& data);
void AddPainterlyShellGraph(MaterialAssetData& data);

} // namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL
