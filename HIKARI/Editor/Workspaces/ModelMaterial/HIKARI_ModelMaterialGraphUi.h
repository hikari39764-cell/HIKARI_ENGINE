#pragma once

#include <string_view>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

std::vector<std::string_view> OutputPins(MaterialNodeKind kind);
std::vector<std::string_view> InputPins(MaterialNodeKind kind);

const MaterialGraphNodeData *FindNode(const MaterialGraphData &graph,
                                      std::string_view id) noexcept;
MaterialGraphNodeData *FindNode(MaterialGraphData &graph,
                                std::string_view id) noexcept;

MaterialValueType DefaultNodeValueType(MaterialNodeKind kind) noexcept;
std::string_view NodeCategory(MaterialNodeKind kind) noexcept;

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
