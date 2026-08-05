#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>
#include <cmath>

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
#endif

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
