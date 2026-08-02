#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

inline constexpr float kNodeWidth = 260.0f;
inline constexpr float kHeaderHeight = 34.0f;
inline constexpr float kPinRowHeight = 22.0f;
inline constexpr float kNodePadding = 9.0f;
inline constexpr float kCanvasMinimumZoom = 0.35f;
inline constexpr float kCanvasMaximumZoom = 1.35f;
inline constexpr float kInlineControlsMinimumZoom = 0.90f;

enum class NodeCardDetail : uint8_t {
  Overview,
  Compact,
  Authoring,
};

struct NodeCardPresentation {
  std::vector<std::string_view> inputs{};
  std::vector<std::string_view> outputs{};
  float inlineHeight = 0.0f;
  float compactSummaryHeight = 0.0f;
  NodeCardDetail detail = NodeCardDetail::Overview;
  bool collapsed = false;
};

std::vector<std::string_view> OutputPins(MaterialNodeKind kind);
std::vector<std::string_view> InputPins(MaterialNodeKind kind);
MaterialGraphData *EditableGraph(MaterialAssetData &source) noexcept;

const MaterialGraphNodeData *FindNode(const MaterialGraphData &graph,
                                      std::string_view id) noexcept;
MaterialGraphNodeData *FindNode(MaterialGraphData &graph,
                                std::string_view id) noexcept;
const MaterialGraphParameterData *FindParameter(
    const MaterialGraphData &graph, std::string_view id) noexcept;
MaterialGraphParameterData *FindParameter(
    MaterialGraphData &graph, std::string_view id) noexcept;
const MaterialGraphResourceData *FindGraphResource(
    const MaterialGraphData &graph, std::string_view id) noexcept;
MaterialGraphResourceData *FindGraphResource(
    MaterialGraphData &graph, std::string_view id) noexcept;

bool LooksLikeColor(std::string_view displayName,
                    std::string_view id);
bool IsRuntimeDrivenInput(MaterialNodeKind kind,
                          std::string_view pin) noexcept;
bool IsInlineOwnedInput(MaterialNodeKind kind,
                        std::string_view pin) noexcept;
NodeCardPresentation ResolveNodeCardPresentation(
    const MaterialGraphData& graph,
    const MaterialGraphNodeData& node,
    float zoom,
    bool collapsed);
size_t CountNodeInputRows(MaterialNodeKind kind,
                          const std::vector<std::string_view>& inputs,
                          bool inlineControls) noexcept;
std::vector<float> BuildNodeInputVerticalOffsets(
    MaterialNodeKind kind,
    const std::vector<std::string_view>& inputs,
    float contentHeight,
    bool inlineControls,
    float zoom);
uint32_t NodeHeaderColor(MaterialNodeKind kind) noexcept;
uint32_t PinColor(std::string_view pin) noexcept;
std::string PinKey(std::string_view nodeId, std::string_view pin);
float NodeInlineHeight(MaterialNodeKind kind) noexcept;
bool PinHasConnection(const MaterialGraphData &graph,
                       std::string_view nodeId, std::string_view pin,
                       bool output) noexcept;
float NodeLogicalHeight(const MaterialGraphData &graph,
                        const MaterialGraphNodeData &node,
                        bool collapsed);
bool HitNodeAuthoringControls(const MATH::Vec2 &pointer,
                              const MATH::Vec2 &minimum,
                              const MATH::Vec2 &maximum,
                              float headerHeight,
                              float inlineHeight) noexcept;
MaterialGraphResourceData *FindRampResource(
    MaterialGraphData &graph, const MaterialGraphNodeData &node) noexcept;
void DrawRampStrip(const MaterialRampData &ramp, float width, float height);

MaterialValueType DefaultNodeValueType(MaterialNodeKind kind) noexcept;
std::string_view NodeCategory(MaterialNodeKind kind) noexcept;

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
