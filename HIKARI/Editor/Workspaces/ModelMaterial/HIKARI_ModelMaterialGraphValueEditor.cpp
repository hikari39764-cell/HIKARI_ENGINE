#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <utility>
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include "imgui.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

namespace {
const char* kMaterialValueTypeItems = "Float1\0Float2\0Float3\0Float4\0";
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
    if (ImGui::Combo("##ValueType", &index, kMaterialValueTypeItems)) {
        valueType = MaterialValueTypeFromIndex(index);
        return true;
    }
    return false;
}

bool DrawMaterialNodeTypeControl(MaterialGraphNodeData& node, MaterialGraphParameterData* parameter) {
    if (!DrawMaterialValueTypeCombo(node.valueType)) {
        return false;
    }
    node.value.type = node.valueType;
    if (parameter != nullptr) {
        parameter->defaultValue.type = node.valueType;
    }
    return true;
}

bool DrawCompactMaterialValue(MaterialValueData& value, bool color) {
    switch (value.type) {
    case MaterialValueType::Bool: return ImGui::Checkbox("##Value", &value.boolean);
    case MaterialValueType::Float1: return ImGui::DragFloat("##Value", &value.numeric.x, 0.01f);
    case MaterialValueType::Float2: return ImGui::DragFloat2("##Value", &value.numeric.x, 0.01f);
    case MaterialValueType::Float3:
        return color ? ImGui::ColorEdit3("##Value", &value.numeric.x,
                                         ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs |
                                             ImGuiColorEditFlags_NoLabel)
                     : ImGui::DragFloat3("##Value", &value.numeric.x, 0.01f);
    case MaterialValueType::Float4:
        return color ? ImGui::ColorEdit4("##Value", &value.numeric.x,
                                         ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs |
                                             ImGuiColorEditFlags_NoLabel)
                     : ImGui::DragFloat4("##Value", &value.numeric.x, 0.01f);
    default: ImGui::TextDisabled("%s", ToString(value.type).data()); return false;
    }
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawGraphParameter(MaterialAssetData& source, MaterialGraphData& graph,
                                                          const MaterialGraphNodeData* node, std::string_view id,
                                                          const char* label, float width, std::string_view pin,
                                                          const MaterialValueData* fallback) {
    namespace UI = MATERIAL_GRAPH_UI;
    auto* parameter = FindMaterialGraphParameter(graph, id);
    if (!parameter && !fallback) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.s.missing.f1ed86").c_str(), label);
        return false;
    }
    ImGui::PushID(id.data(), id.data() + id.size());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (node && !pin.empty() && !UI::InputUsesParameterDefault(graph, *node, pin, id)) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.linked.a089f6").c_str());
        ImGui::PopID();
        return false;
    }
    ImGui::SetNextItemWidth((std::max)(72.0f, width - ImGui::CalcTextSize(label).x - 18.0f));
    MaterialValueData value = parameter ? parameter->defaultValue : *fallback;
    const std::string editLabel = "Edit " + std::string(parameter ? parameter->displayName : label);
    const bool changed = UI::DrawCompactMaterialValue(
        value, node && parameter && UI::LooksLikeColor(parameter->displayName, parameter->id));
    const bool capturesPointer = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (changed || ImGui::IsItemActive() || ImGui::IsItemDeactivated()) {
        auto before = source;
        if (changed) {
            if (parameter)
                (void)ASSETS::MATERIAL::SetMaterialParameterValue(source, id, value, statusMessage_);
            else {
                MaterialGraphParameterData authored{};
                authored.id = id;
                authored.displayName = label;
                authored.defaultValue = value;
                authored.exposed = true;
                graph.parameters.push_back(std::move(authored));
            }
        }
        TrackMaterialItem(std::move(before), editLabel, changed);
    }
    if (!parameter && !changed && ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "%s", TEXT::Localize("editor.ui.runtime.default.editing.creates.the.authored.parameter.c91fef").c_str());
    ImGui::PopID();
    return capturesPointer;
}

} // namespace HIKARI::EDITOR
