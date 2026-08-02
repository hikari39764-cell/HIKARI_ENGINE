#pragma once

#include <string_view>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

bool InputUsesParameterDefault(
    const MaterialGraphData &graph, const MaterialGraphNodeData &node,
    std::string_view pin, std::string_view parameterId);

bool InputUsesResourceDefault(
    const MaterialGraphData &graph, const MaterialGraphNodeData &node,
    std::string_view pin, std::string_view resourceId);

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
