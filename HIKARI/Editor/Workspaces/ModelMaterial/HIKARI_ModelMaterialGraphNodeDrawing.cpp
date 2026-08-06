#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

#if defined(HIKARI_WITH_EDITOR)
NodeCollapseControlResult DrawNodeCollapseControl(
    const MATH::Vec2 &topRight, float screenScale, bool collapsed) {
  const float controlSize = (std::max)(14.0f, 20.0f * screenScale);
  const ImVec2 minimum{
      topRight.x - controlSize - 4.0f,
      topRight.y +
          ((kHeaderHeight * screenScale - controlSize) * 0.5f)};
  ImGui::SetCursorScreenPos(minimum);
  const bool toggled = ImGui::InvisibleButton(
      "##CollapseNode", {controlSize, controlSize});
  const bool hovered = ImGui::IsItemHovered();
  const ImU32 color = hovered
      ? IM_COL32(255, 255, 255, 255)
      : IM_COL32(225, 232, 240, 230);
  const ImVec2 center{
      minimum.x + controlSize * 0.5f,
      minimum.y + controlSize * 0.5f};
  const float arrow = (std::max)(3.0f, 4.0f * screenScale);
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  if (collapsed) {
    drawList->AddTriangleFilled(
        {center.x - arrow * 0.5f, center.y - arrow},
        {center.x - arrow * 0.5f, center.y + arrow},
        {center.x + arrow, center.y}, color);
  } else {
    drawList->AddTriangleFilled(
        {center.x - arrow, center.y - arrow * 0.5f},
        {center.x + arrow, center.y - arrow * 0.5f},
        {center.x, center.y + arrow}, color);
  }
  return {toggled, hovered || ImGui::IsItemActive()};
}

void DrawPendingLink(const MATH::Vec2 &start, const MATH::Vec2 &end,
                     float zoom, uint32_t color) {
  const float tangent = (std::max)(
      60.0f * zoom, std::abs(end.x - start.x) * 0.45f);
  ImGui::GetWindowDrawList()->AddBezierCubic(
      {start.x, start.y}, {start.x + tangent, start.y},
      {end.x - tangent, end.y}, {end.x, end.y}, color, 2.5f);
}

void DrawMaterialGraphGroups(
    ImDrawList *drawList,
    const MaterialGraphData &graph,
    const std::unordered_map<std::string, ImVec2> &screenPositions) {
  constexpr float kGroupPadding = 18.0f;
  constexpr float kGroupTitleHeight = 26.0f;
  constexpr float kGroupNodeHeight = 120.0f;
  for (const MaterialGraphGroupData &group : graph.groups) {
    float minX = (std::numeric_limits<float>::max)();
    float minY = (std::numeric_limits<float>::max)();
    float maxX = -(std::numeric_limits<float>::max)();
    float maxY = -(std::numeric_limits<float>::max)();
    bool hasMembers = false;
    for (const std::string &nodeId : group.nodeIds) {
      const auto found = screenPositions.find(nodeId);
      if (found == screenPositions.end()) {
        continue;
      }
      minX = (std::min)(minX, found->second.x);
      minY = (std::min)(minY, found->second.y);
      maxX = (std::max)(maxX, found->second.x + kNodeWidth);
      maxY = (std::max)(maxY, found->second.y + kGroupNodeHeight);
      hasMembers = true;
    }
    if (!hasMembers) {
      continue;
    }
    const ImVec2 rectMin{
        minX - kGroupPadding,
        minY - kGroupTitleHeight - 6.0f};
    const ImVec2 rectMax{
        maxX + kGroupPadding,
        maxY + kGroupPadding};
    const uint32_t fill = IM_COL32(
        (group.color >> 16u) & 0xffu,
        (group.color >> 8u) & 0xffu,
        group.color & 0xffu,
        (group.color >> 24u) & 0xffu);
    drawList->AddRectFilled(rectMin, rectMax, fill, 8.0f);
    drawList->AddRect(
        rectMin, rectMax,
        IM_COL32(190, 205, 220, 200), 8.0f);
    drawList->AddText(
        {rectMin.x + 10.0f, rectMin.y + 5.0f},
        IM_COL32(235, 242, 250, 255),
        group.name.c_str());
  }
}
#endif

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
