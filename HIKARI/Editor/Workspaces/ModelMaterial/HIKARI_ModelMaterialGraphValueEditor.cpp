#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

#if defined(HIKARI_WITH_EDITOR)
bool DrawCompactMaterialValue(MaterialValueData &value, bool color) {
  switch (value.type) {
  case MaterialValueType::Bool:
    return ImGui::Checkbox("##Value", &value.boolean);
  case MaterialValueType::Float1:
    return ImGui::DragFloat("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float2:
    return ImGui::DragFloat2("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float3:
    return color
        ? ImGui::ColorEdit3("##Value", &value.numeric.x,
                            ImGuiColorEditFlags_Float |
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel)
        : ImGui::DragFloat3("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float4:
    return color
        ? ImGui::ColorEdit4("##Value", &value.numeric.x,
                            ImGuiColorEditFlags_Float |
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel)
        : ImGui::DragFloat4("##Value", &value.numeric.x, 0.01f);
  default:
    ImGui::TextDisabled("%s", ToString(value.type).data());
    return false;
  }
}
#endif

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
