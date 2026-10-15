#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Core/Math/HIKARI_Math3D.h"

#include "imgui.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

inline constexpr float kNodeWidth = 300.0f;
inline constexpr float kHeaderHeight = 34.0f;
inline constexpr float kPinRowHeight = 22.0f;
inline constexpr float kNodePadding = 9.0f;
inline constexpr float kCanvasMinimumZoom = 0.35f;
inline constexpr float kCanvasMaximumZoom = 1.35f;
inline constexpr float kInlineControlsMinimumZoom = 0.78f;
inline constexpr float kInlineControlRowHeight = 25.0f;
inline constexpr float kInlineControlSafetyPadding = 24.0f;

inline std::string JoinPins(const std::vector<std::string_view>& pins, std::string_view fallback) {
    if (pins.empty()) {
        return std::string(fallback);
    }
    std::string result;
    for (const std::string_view pin : pins) {
        if (!result.empty()) {
            result += ", ";
        }
        result += pin;
    }
    return result;
}

inline std::optional<MaterialValueType> ParseMaterialValueType(const std::string& input) {
    std::string normalized = input;
    for (char& c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (normalized == "float1" || normalized == "f1" || normalized == "scalar" || normalized == "1") {
        return MaterialValueType::Float1;
    }
    if (normalized == "float2" || normalized == "f2" || normalized == "vector2" || normalized == "vec2" ||
        normalized == "2") {
        return MaterialValueType::Float2;
    }
    if (normalized == "float3" || normalized == "f3" || normalized == "vector3" || normalized == "vec3" ||
        normalized == "3") {
        return MaterialValueType::Float3;
    }
    if (normalized == "float4" || normalized == "f4" || normalized == "vector4" || normalized == "vec4" ||
        normalized == "color" || normalized == "rgba" || normalized == "4") {
        return MaterialValueType::Float4;
    }
    return std::nullopt;
}

struct NodeInlineLayoutSpec {
    uint32_t rowCount = 0u;
    float fixedHeight = 0.0f;
};

struct NodeCollapseControlResult {
    bool toggled = false;
    bool capturesPointer = false;
};

bool DrawCompactMaterialValue(MaterialValueData& value, bool color);
NodeCollapseControlResult DrawNodeCollapseControl(const MATH::Vec2& topRight, float screenScale, bool collapsed);
void DrawPendingLink(const MATH::Vec2& start, const MATH::Vec2& end, float zoom, uint32_t color);

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

std::vector<std::string_view> InputPins(const MaterialNodeTypeId& typeId);
std::vector<std::string_view> InputPins(const MaterialGraphNodeData& node);
MaterialGraphData* EditableGraph(MaterialAssetData& source) noexcept;

bool InputUsesParameterDefault(const MaterialGraphData& graph, const MaterialGraphNodeData& node, std::string_view pin,
                               std::string_view parameterId);

bool InputUsesResourceDefault(const MaterialGraphData& graph, const MaterialGraphNodeData& node, std::string_view pin,
                              std::string_view resourceId);

bool LooksLikeColor(std::string_view displayName, std::string_view id);
bool DrawMaterialValueTypeCombo(MaterialValueType& valueType);
bool DrawMaterialNodeTypeControl(MaterialGraphNodeData& node, MaterialGraphParameterData* parameter);
void DrawMaterialGraphGroups(ImDrawList* drawList, const MaterialGraphData& graph,
                             const std::unordered_map<std::string, ImVec4>& screenBounds);
bool IsRuntimeDrivenInput(const MaterialNodeTypeId& typeId, std::string_view pin) noexcept;
bool IsInlineOwnedInput(const MaterialNodeTypeId& typeId, std::string_view pin) noexcept;
NodeCardPresentation ResolveNodeCardPresentation(const MaterialGraphData& graph, const MaterialGraphNodeData& node,
                                                 float zoom, bool collapsed);
size_t CountNodeInputRows(const MaterialNodeTypeId& typeId, const std::vector<std::string_view>& inputs,
                          bool inlineControls) noexcept;
std::vector<float> BuildNodeInputVerticalOffsets(const MaterialNodeTypeId& typeId,
                                                 const std::vector<std::string_view>& inputs, float contentHeight,
                                                 bool inlineControls, float zoom);
uint32_t NodeHeaderColor(const MaterialNodeTypeId& typeId) noexcept;
uint32_t PinColor(std::string_view pin) noexcept;
std::string PinKey(std::string_view nodeId, std::string_view pin);
float NodeInlineHeight(const MaterialNodeTypeId& typeId) noexcept;
NodeInlineLayoutSpec ResolveNodeInlineLayoutSpec(const MaterialNodeTypeId& typeId) noexcept;
bool PinHasConnection(const MaterialGraphData& graph, std::string_view nodeId, std::string_view pin,
                      bool output) noexcept;
MATH::Vec2 NodeLayoutSize(const MaterialGraphData& graph, const MaterialGraphNodeData& node);
bool AutoLayoutMaterialGraph(MaterialGraphData& graph, const std::vector<std::string>& selection = {});
float NodeLogicalHeight(const MaterialGraphData& graph, const MaterialGraphNodeData& node, bool collapsed);
bool HitNodeAuthoringControls(const MATH::Vec2& pointer, const MATH::Vec2& minimum, const MATH::Vec2& maximum,
                              float headerHeight, float inlineHeight) noexcept;
MaterialGraphResourceData* FindRampResource(MaterialGraphData& graph, const MaterialGraphNodeData& node) noexcept;
void DrawRampStrip(const MaterialRampData& ramp, float width, float height);

MaterialValueType DefaultNodeValueType(const MaterialNodeTypeId& typeId) noexcept;
std::string_view NodeMathSubgroup(MaterialNodeKind kind);
std::string NodeDisplayName(const MaterialNodeTypeId& typeId);
std::string_view NodeCategory(const MaterialNodeTypeId& typeId) noexcept;

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
