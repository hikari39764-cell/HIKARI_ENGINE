#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include <algorithm>
#include <cstdio>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawMaterialGraphInspector(DocumentSceneBase& scene, MaterialAssetData& source,
                                                                  MaterialGraphData& graph) {
#if defined(HIKARI_WITH_EDITOR)
    if (MaterialGraphNodeData* node = FindMaterialGraphNode(graph, selectedGraphNodeId_)) {
        const auto name = node->displayName.empty() || node->displayName == ToString(node->kind)
                              ? MATERIAL_GRAPH_UI::NodeDisplayName(node->kind)
                              : node->displayName;
        ImGui::SeparatorText(name.c_str());
        if (const float height = MATERIAL_GRAPH_UI::NodeInlineHeight(node->kind); height > 0.0f) {
            ImGui::BeginChild("##NodeProperties", {0.0f, height + 8.0f}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const auto origin = ImGui::GetCursorScreenPos();
            DrawMaterialGraphNodeInlineControls(scene, source, graph, *node, origin.x, origin.y,
                                                ImGui::GetContentRegionAvail().x);
            ImGui::Dummy({1.0f, height});
            ImGui::EndChild();
        }
        if (node->kind == MaterialNodeKind::Parameter) {
            if (auto* parameter = FindMaterialGraphParameter(graph, node->referenceId)) {
                if (ImGui::TreeNode(TEXT::LocalizeCStr("material.parameter.presentation"))) {
                    const auto editText = [&](const char* key, std::string& value) {
                        char buffer[256]{};
                        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
                        MaterialAssetData before = source;
                        const bool changed = ImGui::InputText(TEXT::LocalizeCStr(key), buffer, sizeof(buffer));
                        if (changed)
                            value = buffer;
                        TrackMaterialItem(std::move(before), "Name material parameter", changed);
                    };
                    editText("material.parameter.name", parameter->displayName);
                    editText("material.parameter.group", parameter->group);
                    MaterialAssetData before = source;
                    if (ImGui::Checkbox(TEXT::LocalizeCStr("material.parameter.expose"), &parameter->exposed))
                        authoringDocument_.CommitMaterial(std::move(before), "Expose material parameter");
                    before = source;
                    if (ImGui::Checkbox(TEXT::LocalizeCStr("material.parameter.range"), &parameter->useUiRange)) {
                        if (parameter->useUiRange && !(parameter->uiMinimum < parameter->uiMaximum)) {
                            parameter->uiMinimum = 0.0f;
                            parameter->uiMaximum = 1.0f;
                        }
                        authoringDocument_.CommitMaterial(std::move(before), "Set parameter range");
                    }
                    if (parameter->useUiRange) {
                        before = source;
                        const bool changed =
                            ImGui::DragFloatRange2(TEXT::LocalizeCStr("material.parameter.limits"),
                                                   &parameter->uiMinimum, &parameter->uiMaximum, 0.01f);
                        if (changed && parameter->uiMinimum >= parameter->uiMaximum)
                            parameter->uiMaximum = parameter->uiMinimum + 0.001f;
                        TrackMaterialItem(std::move(before), "Set parameter range", changed);
                    }
                    ImGui::TreePop();
                }
            }
        }
        auto outputs = GetMaterialGraphNodeOutputPins(*node);
        std::erase_if(outputs, [&](auto pin) {
            const auto type = ResolveMaterialGraphPinType(graph, *node, pin, true);
            return !type || !IsMaterialNumericType(*type);
        });
        if (!outputs.empty() && source.domain == MaterialDomain::Surface) {
            const bool inspecting = nodePreviewId_ == node->id;
            const std::string pin = inspecting ? nodePreviewPin_ : std::string(outputs.front());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##InspectOutput", pin.c_str())) {
                for (const auto candidate : outputs) {
                    const auto type = ResolveMaterialGraphPinType(graph, *node, candidate, true);
                    if (type && IsMaterialNumericType(*type) && ImGui::Selectable(candidate.data(), candidate == pin)) {
                        nodePreviewId_ = node->id;
                        nodePreviewPin_ = candidate;
                        nodePreviewRefreshPending_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (IconToggleButton(EditorGlyph::Visible, "##inspect", inspecting, {},
                                 TEXT::LocalizeCStr("material.inspect.node"))) {
                nodePreviewId_ = inspecting ? "" : node->id;
                nodePreviewPin_ = pin;
                nodePreviewRefreshPending_ = true;
            }
            if (inspecting) {
                ImGui::SetNextItemWidth(120.0f);
                const char* channels[]{"RGB / UV", "R / X", "G / Y", "B / Z", "A / W"};
                int channel = nodePreviewChannel_ + 1;
                if (ImGui::Combo("##InspectChannel", &channel, channels, 5)) {
                    nodePreviewChannel_ = channel - 1;
                    nodePreviewRefreshPending_ = true;
                }
                ImGui::TextWrapped("%s", TEXT::LocalizeCStr("material.inspect.local"));
            }
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeCStr("material.inspector.connections"))) {
            for (const auto& edge : graph.edges) {
                if (edge.targetNodeId != node->id)
                    continue;
                const auto* upstream = FindMaterialGraphNode(graph, edge.sourceNodeId);
                const std::string label =
                    edge.targetPin + "  ←  " +
                    (upstream ? MATERIAL_GRAPH_UI::NodeDisplayName(upstream->kind) : edge.sourceNodeId);
                if (ImGui::Selectable((label + "##" + edge.id).c_str())) {
                    SetGraphNodeSelection(edge.sourceNodeId, false);
                    graphFocusRequested_ = true;
                }
            }
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeCStr("material.inspector.advanced"))) {
            ImGui::TextDisabled("%s", node->id.c_str());
            const auto& contract = ASSETS::MATERIAL::GetMaterialNodeSemanticContract(node->kind);
            ImGui::TextDisabled(TEXT::Localize("editor.ui.connect.to.44ad75").c_str());
            ImGui::TextWrapped("%.*s", static_cast<int>(contract.connectsTo.size()), contract.connectsTo.data());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.affected.by.9caebf").c_str());
            ImGui::TextWrapped("%.*s", static_cast<int>(contract.affectedBy.size()), contract.affectedBy.data());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.provides.1f0fb2").c_str());
            ImGui::TextWrapped("%.*s", static_cast<int>(contract.provides.size()), contract.provides.data());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.default.failure.dc6da3").c_str());
            ImGui::TextWrapped("%.*s", static_cast<int>(contract.defaultBehavior.size()),
                               contract.defaultBehavior.data());
            ImGui::TextWrapped("%.*s", static_cast<int>(contract.failureBehavior.size()),
                               contract.failureBehavior.data());
        }
    } else {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.current.node.85de31").c_str());
        ImGui::TextWrapped("%s", TEXT::LocalizeCStr("material.inspector.select"));
    }

    if (!selectedGraphEdgeId_.empty()) {
        const auto selectedEdge = std::find_if(graph.edges.begin(), graph.edges.end(),
                                               [this](const auto& edge) { return edge.id == selectedGraphEdgeId_; });
        if (selectedEdge != graph.edges.end()) {
            ImGui::SeparatorText(TEXT::Localize("editor.ui.selected.connection.b1be4d").c_str());
            ImGui::TextWrapped("%s.%s -> %s.%s", selectedEdge->sourceNodeId.c_str(), selectedEdge->sourcePin.c_str(),
                               selectedEdge->targetNodeId.c_str(), selectedEdge->targetPin.c_str());
        }
    }
    ImGui::BeginDisabled(selectedGraphEdgeId_.empty());
    const bool requestDeleteEdge =
        ImGui::Button(TEXT::LocalizeWindow("ui.materialGraph.deleteConnection", "Delete Connection").c_str());
    ImGui::EndDisabled();

    const MaterialCompileResult& compile = authoringDocument_.GetCompileResult();
    const uint64_t materialRevision = authoringDocument_.GetMaterialRevision();
    const uint64_t compileRevision = authoringDocument_.GetCompileRevision();
    const bool currentProgram = compile.success && materialRevision == compileRevision;
    const ImVec4 statusColor = currentProgram ? ImVec4{0.28f, 0.82f, 0.48f, 1.0f} : ImVec4{1.0f, 0.30f, 0.30f, 1.0f};
    ImGui::SeparatorText(TEXT::Localize("editor.ui.runtime.preview.status.897160").c_str());
    ImGui::TextColored(statusColor, "%s",
                       TEXT::LocalizeCStr(currentProgram    ? "material.preview.live"
                                          : compile.success ? "material.preview.pending"
                                                            : "material.preview.invalid"));
    if (ImGui::CollapsingHeader(
            TEXT::LocalizeWindow("editor.ui.compile.diagnostics.6be167", "Compile & Diagnostics").c_str(),
            currentProgram ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(compile.success ? TEXT::LocalizeCStr("editor.ui.compiled.14878f")
                                               : TEXT::LocalizeCStr("editor.ui.compile.failed.97203d"));
        for (const MaterialCompileDiagnostic& diagnostic : compile.diagnostics) {
            ImGui::PushID(diagnostic.subjectId.c_str());
            if (FindMaterialGraphNode(graph, diagnostic.subjectId)) {
                if (ImGui::Selectable(diagnostic.message.c_str())) {
                    SetGraphNodeSelection(diagnostic.subjectId, false);
                    graphFocusRequested_ = true;
                }
            } else
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            ImGui::PopID();
        }
        if (ImGui::TreeNode(TEXT::LocalizeCStr("editor.ui.expression.budget.dcb8b8"))) {
            if (!compile.success) {
                ImGui::TextUnformatted(
                    TEXT::Localize("editor.ui.not.evaluated.current.graph.does.not.compile.238dc7").c_str());
            } else {
                RENDER3D::MATERIAL::MaterialExpressionBudget budget{};
                std::string budgetError{};
                if (!RENDER3D::MATERIAL::ResolveMaterialExpressionBudget(compile.program, budget, budgetError)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                                       TEXT::Localize("editor.ui.budget.resolve.failed.s.1f2ebe").c_str(),
                                       budgetError.c_str());
                } else if (!budget.active) {
                    ImGui::TextUnformatted(
                        TEXT::Localize("editor.ui.fixed.stylized.response.only.no.pixel.expression.graph.6cbf9c")
                            .c_str());
                } else {
                    const auto budgetBar = [](const char* label, uint32_t used, uint32_t maxValue) {
                        ImGui::Text("%s: %u / %u", label, used, maxValue);
                        ImGui::SameLine();
                        ImGui::ProgressBar(maxValue > 0u ? static_cast<float>(used) / static_cast<float>(maxValue)
                                                         : 0.0f,
                                           ImVec2(-1.0f, 0.0f), "");
                    };
                    budgetBar(TEXT::LocalizeCStr("material.inspector.instructions"), budget.instructionCount,
                              budget.maxInstructionCount);
                    budgetBar(TEXT::LocalizeCStr("material.inspector.registers"), budget.registerCount,
                              budget.maxRegisterCount);
                    budgetBar(TEXT::LocalizeCStr("material.inspector.constants"), budget.valueCount,
                              budget.maxValueCount);
                    budgetBar(TEXT::LocalizeCStr("material.inspector.slots"), budget.parameterSlotCount,
                              budget.maxParameterSlotCount);
                    budgetBar(TEXT::LocalizeCStr("material.inspector.resources"), budget.resourceCount,
                              budget.maxResourceSlotCount);
                    if (!budget.nodeInstructionCounts.empty()) {
                        ImGui::TextDisabled(TEXT::Localize("editor.ui.per.node.instruction.cost.51d8c4").c_str());
                        for (const auto& [nodeId, instructionCount] : budget.nodeInstructionCounts) {
                            if (instructionCount > 0u) {
                                ImGui::BulletText("%s: %u", nodeId.c_str(), instructionCount);
                            }
                        }
                    }
                }
            }
            ImGui::TreePop();
        }
    }
    return requestDeleteEdge;
#else
    (void)scene;
    (void)source;
    (void)graph;
    return false;
#endif
}

} // namespace HIKARI::EDITOR
