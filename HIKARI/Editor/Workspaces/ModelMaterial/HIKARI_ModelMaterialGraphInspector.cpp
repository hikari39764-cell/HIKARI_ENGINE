#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <string>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawMaterialGraphInspector(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr float kInspectorWidth = 260.0f;
  ImGui::BeginChild("##MaterialGraphInspector", ImVec2(kInspectorWidth, 0.0f),
                    true);
  ImGui::TextDisabled("NODES  %zu", graph.nodes.size());
  for (const MaterialGraphNodeData &node : graph.nodes) {
    const std::string label =
        (node.displayName.empty() ? std::string(ToString(node.kind))
                                  : node.displayName) +
        "##" + node.id;
    if (ImGui::Selectable(label.c_str(), selectedGraphNodeId_ == node.id)) {
      selectedGraphNodeId_ = node.id;
      selectedGraphEdgeId_.clear();
    }
  }

  if (MaterialGraphNodeData *node =
          MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_)) {
    ImGui::SeparatorText("Selected Node");
    ImGui::TextWrapped("%s", node->id.c_str());
    ImGui::TextDisabled("%s / %s",
                        MATERIAL_GRAPH_UI::NodeCategory(node->kind).data(),
                        ToString(node->kind).data());
    const bool editableType =
        node->kind == MaterialNodeKind::Constant ||
        node->kind == MaterialNodeKind::Add ||
        node->kind == MaterialNodeKind::Multiply ||
        node->kind == MaterialNodeKind::Lerp ||
        node->kind == MaterialNodeKind::Clamp ||
        node->kind == MaterialNodeKind::Saturate ||
        node->kind == MaterialNodeKind::OneMinus ||
        node->kind == MaterialNodeKind::Power ||
        node->kind == MaterialNodeKind::FunctionCall;
    if (editableType &&
        ImGui::BeginCombo("Value Type", ToString(node->valueType).data())) {
      for (uint32_t value = static_cast<uint32_t>(MaterialValueType::Float1);
           value <= static_cast<uint32_t>(MaterialValueType::SpecularResponse);
           ++value) {
        const auto type = static_cast<MaterialValueType>(value);
        if (ImGui::Selectable(ToString(type).data(), type == node->valueType)) {
          MaterialAssetData before = source;
          node->valueType = type;
          if (node->kind == MaterialNodeKind::Constant) {
            node->value.type = type;
          }
          CommitMaterialMutation(std::move(before), "Change node value type",
                                 scene);
        }
      }
      ImGui::EndCombo();
    }
    if (node->kind == MaterialNodeKind::Parameter) {
      const char *preview = node->referenceId.empty()
                                ? "Choose parameter"
                                : node->referenceId.c_str();
      if (ImGui::BeginCombo("Parameter", preview)) {
        for (const MaterialGraphParameterData &parameter : graph.parameters) {
          if (ImGui::Selectable(parameter.displayName.empty()
                                    ? parameter.id.c_str()
                                    : parameter.displayName.c_str(),
                                parameter.id == node->referenceId)) {
            MaterialAssetData before = source;
            node->referenceId = parameter.id;
            node->valueType = parameter.defaultValue.type;
            CommitMaterialMutation(std::move(before), "Bind parameter node",
                                   scene);
          }
        }
        ImGui::EndCombo();
      }
    } else if (node->kind == MaterialNodeKind::TextureSample) {
      const char *preview = node->referenceId.empty()
                                ? "Choose resource"
                                : node->referenceId.c_str();
      if (ImGui::BeginCombo("Resource", preview)) {
        for (const MaterialGraphResourceData &resource : graph.resources) {
          if (ImGui::Selectable(resource.displayName.empty()
                                    ? resource.id.c_str()
                                    : resource.displayName.c_str(),
                                resource.id == node->referenceId)) {
            MaterialAssetData before = source;
            node->referenceId = resource.id;
            CommitMaterialMutation(std::move(before), "Bind texture sample",
                                   scene);
          }
        }
        ImGui::EndCombo();
      }
    } else if (node->kind == MaterialNodeKind::FunctionCall) {
      const AssetChoice *choice = FindMaterialChoice(node->assetReference);
      const char *preview = choice != nullptr ? choice->label.c_str()
                            : node->assetReference.IsValid()
                                ? "Missing function"
                                : "Choose function";
      if (ImGui::BeginCombo("Function", preview)) {
        for (const AssetChoice &candidate : materialChoices_) {
          if (candidate.materialKind != MaterialAssetKind::Function) {
            continue;
          }
          if (ImGui::Selectable(candidate.label.c_str(),
                                candidate.guid == node->assetReference)) {
            MaterialAssetData before = source;
            node->assetReference = candidate.guid;
            CommitMaterialMutation(std::move(before), "Bind material function",
                                   scene);
          }
        }
        ImGui::EndCombo();
      }
    }
  }

  ImGui::SeparatorText("Connections");
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    const std::string label = edge.sourceNodeId + "." + edge.sourcePin +
                              " -> " + edge.targetNodeId + "." +
                              edge.targetPin + "##" + edge.id;
    if (ImGui::Selectable(label.c_str(), selectedGraphEdgeId_ == edge.id)) {
      selectedGraphEdgeId_ = edge.id;
      selectedGraphNodeId_.clear();
    }
  }
  ImGui::BeginDisabled(selectedGraphEdgeId_.empty());
  const bool requestDeleteEdge = ImGui::Button("Delete Connection");
  ImGui::EndDisabled();

  const MaterialCompileResult &compile = authoringDocument_.GetCompileResult();
  ImGui::SeparatorText("Compile State");
  ImGui::Text("Revision %llu / %llu",
              static_cast<unsigned long long>(
                  authoringDocument_.GetMaterialRevision()),
              static_cast<unsigned long long>(
                  authoringDocument_.GetCompileRevision()));
  ImGui::TextWrapped("%s%s%s", compile.success ? "Compiled" : "Compile failed",
                     compile.usedLastGood ? " | last-good" : "",
                     compile.usedFallback ? " | PBR fallback" : "");
  for (const MaterialCompileDiagnostic &diagnostic : compile.diagnostics) {
    ImGui::BulletText("%s: %s", diagnostic.code.c_str(),
                      diagnostic.message.c_str());
  }
  ImGui::EndChild();
  return requestDeleteEdge;
#else
  (void)scene;
  (void)source;
  (void)graph;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
