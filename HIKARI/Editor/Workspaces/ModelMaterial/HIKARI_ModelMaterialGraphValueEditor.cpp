#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

#if defined(HIKARI_WITH_EDITOR)
namespace {
const char* kMaterialValueTypeItems =
    "Float1\0Float2\0Float3\0Float4\0";
int MaterialValueTypeIndex(MaterialValueType type) {
    switch (type) {
    case MaterialValueType::Float2: return 1;
    case MaterialValueType::Float3: return 2;
    case MaterialValueType::Float4: return 3;
    case MaterialValueType::Float1:
    default: return 0;
    }
}
MaterialValueType MaterialValueTypeFromIndex(int index) {
    switch (index) {
    case 1: return MaterialValueType::Float2;
    case 2: return MaterialValueType::Float3;
    case 3: return MaterialValueType::Float4;
    case 0:
    default: return MaterialValueType::Float1;
    }
}
} // namespace

bool DrawMaterialValueTypeCombo(MaterialValueType& valueType) {
    int index = MaterialValueTypeIndex(valueType);
    if (ImGui::Combo(
            "##ValueType", &index, kMaterialValueTypeItems)) {
        valueType = MaterialValueTypeFromIndex(index);
        return true;
    }
    return false;
}

bool DrawMaterialNodeTypeControl(
    MaterialGraphNodeData& node,
    MaterialGraphParameterData* parameter) {
    if (!DrawMaterialValueTypeCombo(node.valueType)) {
        return false;
    }
    node.value.type = node.valueType;
    if (parameter != nullptr) {
        parameter->defaultValue.type = node.valueType;
    }
    return true;
}

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
