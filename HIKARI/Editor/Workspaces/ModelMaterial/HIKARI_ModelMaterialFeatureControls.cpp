#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <array>
#include <map>

#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawMaterialParameterControls(MaterialAssetData& source,
                                                                     const MaterialGraphData& graph,
                                                                     const MaterialAssetData* parent) {
    ImGui::PushID("MaterialParameterControls");
    const auto effectiveValue = [&](const MaterialGraphParameterData& parameter) {
        MaterialValueData value = parameter.defaultValue;
        if (parent) {
            const auto entry =
                std::find_if(source.instance.parameterOverrides.begin(), source.instance.parameterOverrides.end(),
                             [&](const auto& item) { return item.parameterId == parameter.id; });
            if (entry != source.instance.parameterOverrides.end())
                value = entry->value;
        }
        return value;
    };
    std::map<std::string, std::vector<const MaterialGraphParameterData*>> groups;
    for (const auto& parameter : graph.parameters)
        if (parameter.exposed)
            groups[parameter.group].push_back(&parameter);
    for (const auto& [group, parameters] : groups) {
        const auto label = TEXT::Localize(group.empty() ? "material.parameter.general" : group);
        const bool expanded =
            group.empty() || group == "material.painterly.groupStroke" || group == "material.painterly.groupShell";
        if (!ImGui::CollapsingHeader(label.c_str(), expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            continue;
        for (const auto* parameter : parameters) {
            ImGui::PushID(parameter->id.c_str());
            ImGui::TextUnformatted(
                TEXT::Localize(parameter->displayName.empty() ? parameter->id : parameter->displayName).c_str());
            MaterialValueData value = effectiveValue(*parameter);
            const auto shellInput = std::find_if(graph.edges.begin(), graph.edges.end(), [&](const auto& edge) {
                const auto* input = FindMaterialGraphNode(graph, edge.sourceNodeId);
                const auto* target = FindMaterialGraphNode(graph, edge.targetNodeId);
                return input && target && input->kind == MaterialNodeKind::Parameter &&
                       input->referenceId == parameter->id && target->kind == MaterialNodeKind::Shell;
            });
            const bool layerCount = shellInput != graph.edges.end() && shellInput->targetPin == "MaxLayers";
            ImGui::SetNextItemWidth(parent ? -64.0f : -1.0f);
            MaterialAssetData before = source;
            bool changed = false;
            if (layerCount) {
                constexpr std::array values{0, 2, 4, 8};
                const auto preview = std::to_string(static_cast<int>(value.numeric.x));
                if (ImGui::BeginCombo("##Value", preview.c_str())) {
                    for (const int layers : values)
                        if (ImGui::Selectable(std::to_string(layers).c_str(),
                                              value.numeric.x == static_cast<float>(layers))) {
                            value.numeric.x = static_cast<float>(layers);
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
            } else if (parameter->useUiRange && value.type == MaterialValueType::Float1) {
                float minimum = parameter->uiMinimum, maximum = parameter->uiMaximum;
                if (shellInput != graph.edges.end() &&
                    (shellInput->targetPin == "LodStartPixels" || shellInput->targetPin == "LodFullPixels")) {
                    const bool start = shellInput->targetPin == "LodStartPixels";
                    const auto sibling = std::find_if(graph.edges.begin(), graph.edges.end(), [&](const auto& edge) {
                        return edge.targetNodeId == shellInput->targetNodeId &&
                               edge.targetPin == (start ? "LodFullPixels" : "LodStartPixels");
                    });
                    const auto* node =
                        sibling != graph.edges.end() ? FindMaterialGraphNode(graph, sibling->sourceNodeId) : nullptr;
                    const auto* siblingParameter = node && node->kind == MaterialNodeKind::Parameter
                                                       ? FindMaterialGraphParameter(graph, node->referenceId)
                                                       : nullptr;
                    if (siblingParameter) {
                        const float boundary = effectiveValue(*siblingParameter).numeric.x;
                        if (start)
                            maximum = (std::min)(maximum, boundary - 1.0f);
                        else
                            minimum = (std::max)(minimum, boundary + 1.0f);
                    }
                }
                changed = ImGui::SliderFloat("##Value", &value.numeric.x, minimum, (std::max)(minimum, maximum));
            } else {
                changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
                    value, MATERIAL_GRAPH_UI::LooksLikeColor(parameter->displayName, parameter->id));
            }
            const bool applied = changed && ASSETS::MATERIAL::SetMaterialParameterValue(source, parameter->id, value,
                                                                                        statusMessage_, parent);
            if (layerCount) {
                if (applied)
                    authoringDocument_.CommitMaterial(std::move(before), "Set shell layer limit");
            } else {
                TrackMaterialItem(std::move(before), "Edit material parameter", applied);
            }
            if (parent) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TEXT::LocalizeWindow("ui.common.reset", "Reset").c_str())) {
                    auto resetBefore = source;
                    if (ASSETS::MATERIAL::RestoreMaterialInstanceParameterDefault(source, parameter->id))
                        authoringDocument_.CommitMaterial(std::move(resetBefore), "Reset instance parameter");
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

void ModelMaterialWorkspaceController::DrawOutlineThicknessPresets(MaterialAssetData& source,
                                                                   MaterialGraphData& graph) {
    MaterialGraphParameterData* outlineThickness = FindMaterialGraphParameter(graph, "parameter.outlineThickness");
    if (outlineThickness == nullptr) {
        return;
    }
    ImGui::TextDisabled(TEXT::Localize("editor.ui.outline.thickness.presets.a424d6").c_str());
    const auto applyThickness = [&](float thickness, const char* label) {
        if (ImGui::Button(label)) {
            MaterialAssetData before = source;
            outlineThickness->defaultValue.numeric.x = thickness;
            authoringDocument_.CommitMaterial(std::move(before), "Apply outline thickness preset");
        }
    };
    applyThickness(1.0f, "Thin");
    ImGui::SameLine();
    applyThickness(2.0f, "Medium");
    ImGui::SameLine();
    applyThickness(4.0f, "Thick");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            TEXT::Localize("editor.ui.outline.thickness.is.measured.in.screen.pixels.and.clamped.to.0..2901c8")
                .c_str());
    }
}

} // namespace HIKARI::EDITOR
