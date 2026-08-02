#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>

#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

const char *ValueTypeLabel(MaterialValueType type) noexcept {
  switch (type) {
  case MaterialValueType::Bool:
    return "Boolean";
  case MaterialValueType::Float1:
    return "Scalar";
  case MaterialValueType::Float2:
    return "Vector 2";
  case MaterialValueType::Float3:
    return "Color / Vector 3";
  case MaterialValueType::Float4:
    return "Color / Vector 4";
  default:
    return ToString(type).data();
  }
}

bool DrawMaterialValue(const char *label, MaterialValueData &value,
                       bool color) {
  switch (value.type) {
  case MaterialValueType::Bool:
    return ImGui::Checkbox(label, &value.boolean);
  case MaterialValueType::Float1:
    return ImGui::DragFloat(label, &value.numeric.x, 0.01f);
  case MaterialValueType::Float2:
    return ImGui::DragFloat2(label, &value.numeric.x, 0.01f);
  case MaterialValueType::Float3:
    return color ? ImGui::ColorEdit3(label, &value.numeric.x)
                 : ImGui::DragFloat3(label, &value.numeric.x, 0.01f);
  case MaterialValueType::Float4:
    return color ? ImGui::ColorEdit4(label, &value.numeric.x)
                 : ImGui::DragFloat4(label, &value.numeric.x, 0.01f);
  default:
    ImGui::TextDisabled("%s cannot store an inline value",
                        ToString(value.type).data());
    return false;
  }
}

bool IsEditableNodeValueType(MaterialNodeKind kind,
                             MaterialValueType type) noexcept {
  if (kind == MaterialNodeKind::Constant) {
    return type >= MaterialValueType::Bool &&
           type <= MaterialValueType::Float4;
  }
  if (kind != MaterialNodeKind::FunctionCall) {
    return type >= MaterialValueType::Float1 &&
           type <= MaterialValueType::Float4;
  }
  return type != MaterialValueType::Surface;
}

MaterialGraphData *EditableGraph(MaterialAssetData &source) noexcept {
  if (source.kind == MaterialAssetKind::Material) {
    return &source.graph;
  }
  if (source.kind == MaterialAssetKind::Function) {
    return &source.function.graph;
  }
  return nullptr;
}

const char *ResourceColorSpace(
    MaterialResourceSemantic semantic) noexcept {
  switch (semantic) {
  case MaterialResourceSemantic::BaseColor:
  case MaterialResourceSemantic::Emissive:
  case MaterialResourceSemantic::SpecularColor:
  case MaterialResourceSemantic::Ramp:
  case MaterialResourceSemantic::MatCap:
    return "sRGB (semantic)";
  default:
    return "Linear (semantic)";
  }
}

} // namespace

bool ModelMaterialWorkspaceController::DrawMaterialGraphInspector(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph) {
#if defined(HIKARI_WITH_EDITOR)
  if (MaterialGraphNodeData *node =
          MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_)) {
    ImGui::TextDisabled("CURRENT NODE");
    ImGui::TextWrapped("%s",
                       node->displayName.empty()
                           ? ToString(node->kind).data()
                           : node->displayName.c_str());
    ImGui::TextDisabled("%s / %s",
                        MATERIAL_GRAPH_UI::NodeCategory(node->kind).data(),
                        ToString(node->kind).data());
    ImGui::TextDisabled("%s", node->id.c_str());
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
        ImGui::BeginCombo("Data Shape", ValueTypeLabel(node->valueType))) {
      for (uint32_t value = static_cast<uint32_t>(MaterialValueType::Bool);
           value <= static_cast<uint32_t>(MaterialValueType::OutlineResponse);
           ++value) {
        const auto type = static_cast<MaterialValueType>(value);
        if (!IsEditableNodeValueType(node->kind, type)) {
          continue;
        }
        if (ImGui::Selectable(ValueTypeLabel(type), type == node->valueType)) {
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
    if (node->kind == MaterialNodeKind::PbrSurface) {
      ImGui::TextWrapped(
          "This node owns the game surface inputs. Unconnected sockets use "
          "the defaults below; connected sockets override them.");
      DrawMaterialSurfaceControls(scene, source);
    } else if (node->kind == MaterialNodeKind::SurfaceOutput) {
      ImGui::TextWrapped(
          "This is the required render output. Material-level render "
          "features are attached here.");
      DrawMaterialFeatureControls(scene, source);
    } else if (node->kind == MaterialNodeKind::Constant) {
      MaterialAssetData before = source;
      const bool changed = DrawMaterialValue(
          "Value", node->value,
          MATERIAL_GRAPH_UI::LooksLikeColor(node->displayName, node->id));
      const bool continuous = node->value.type != MaterialValueType::Bool;
      if (continuous && ImGui::IsItemActivated() && !materialGesture_) {
        materialGesture_ = MaterialEditGesture{
            std::move(before), "Edit material constant"};
      }
      if (continuous && ImGui::IsItemDeactivatedAfterEdit()) {
        CommitMaterialGesture(scene);
      }
      if (continuous && changed) PreviewMaterialGesture(scene);
      if (!continuous && changed) {
        CommitMaterialMutation(std::move(before),
                               "Edit material constant", scene);
      }
    } else if (node->kind == MaterialNodeKind::Parameter) {
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
      MaterialGraphParameterData *parameter =
          MATERIAL_GRAPH_UI::FindParameter(graph, node->referenceId);
      if (parameter == nullptr) {
        if (ImGui::Button("Create Parameter", ImVec2(-1.0f, 0.0f))) {
          MaterialAssetData before = source;
          const std::string id =
              "Parameter_" + std::to_string(authoringIdCounter_++);
          MaterialGraphParameterData created{};
          created.id = id;
          created.displayName = "Parameter";
          created.defaultValue.type = node->valueType;
          created.exposed = true;
          graph.parameters.push_back(std::move(created));
          node->referenceId = id;
          CommitMaterialMutation(std::move(before),
                                 "Create material parameter", scene);
        }
      } else {
        ImGui::TextDisabled("Stable ID: %s", parameter->id.c_str());
        {
          MaterialAssetData before = source;
          if (ImGui::Checkbox("Expose on material", &parameter->exposed)) {
            CommitMaterialMutation(std::move(before),
                                   "Change parameter exposure", scene);
          }
        }
        if (ImGui::BeginCombo(
                "Parameter Type",
                ValueTypeLabel(parameter->defaultValue.type))) {
          for (uint32_t value =
                   static_cast<uint32_t>(MaterialValueType::Bool);
               value <= static_cast<uint32_t>(MaterialValueType::Float4);
               ++value) {
            const auto type = static_cast<MaterialValueType>(value);
            if (ImGui::Selectable(
                    ValueTypeLabel(type),
                    type == parameter->defaultValue.type)) {
              MaterialAssetData before = source;
              parameter->defaultValue.type = type;
              node->valueType = type;
              CommitMaterialMutation(std::move(before),
                                     "Change parameter type", scene);
            }
          }
          ImGui::EndCombo();
        }
        MaterialAssetData before = source;
        const char *valueLabel = parameter->displayName.empty()
                                     ? "Value"
                                     : parameter->displayName.c_str();
        const bool changed = DrawMaterialValue(
            valueLabel, parameter->defaultValue,
            MATERIAL_GRAPH_UI::LooksLikeColor(
                parameter->displayName, parameter->id));
        const bool continuous =
            parameter->defaultValue.type != MaterialValueType::Bool;
        if (continuous && ImGui::IsItemActivated() && !materialGesture_) {
          materialGesture_ = MaterialEditGesture{
              std::move(before), "Edit material parameter"};
        }
        if (continuous && ImGui::IsItemDeactivatedAfterEdit()) {
          CommitMaterialGesture(scene);
        }
        if (continuous && changed) PreviewMaterialGesture(scene);
        if (!continuous && changed) {
          CommitMaterialMutation(std::move(before),
                                 "Edit material parameter", scene);
        }
      }
    } else if (node->kind == MaterialNodeKind::TextureSample) {
      const char *preview = node->referenceId.empty()
                                ? "Choose resource"
                                : node->referenceId.c_str();
      if (ImGui::BeginCombo("Resource", preview)) {
        for (const MaterialGraphResourceData &resource : graph.resources) {
          if (resource.type != MaterialResourceType::Texture2D &&
              resource.type != MaterialResourceType::TextureCube) {
            continue;
          }
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
      MaterialGraphResourceData *resource =
          MATERIAL_GRAPH_UI::FindGraphResource(graph, node->referenceId);
      if (resource == nullptr) {
        if (ImGui::Button("Create Texture Resource", ImVec2(-1.0f, 0.0f))) {
          MaterialAssetData before = source;
          MaterialGraphResourceData created{};
          created.id = "Texture_" +
                       std::to_string(authoringIdCounter_++);
          created.displayName = "Texture";
          graph.resources.push_back(std::move(created));
          node->referenceId = graph.resources.back().id;
          CommitMaterialMutation(std::move(before),
                                 "Create texture sample resource", scene);
        }
      } else {
        std::string textureGuid = resource->assetGuid.value;
        AssetFieldOptions fieldOptions{};
        fieldOptions.label = "Texture Asset";
        fieldOptions.requiredType = AssetType::Texture;
        if (DrawAssetField(&scene.GetAssetDatabase(), fieldOptions,
                           textureGuid)) {
          MaterialAssetData before = source;
          resource->assetGuid = AssetGuid{ textureGuid };
          resource->enabled = resource->assetGuid.IsValid();
          CommitMaterialMutation(std::move(before),
                                 "Bind texture sample asset", scene);
        }

        if (ImGui::BeginCombo("Semantic",
                              ToString(resource->semantic).data())) {
          for (uint32_t value =
                   static_cast<uint32_t>(MaterialResourceSemantic::Custom);
               value <=
                   static_cast<uint32_t>(MaterialResourceSemantic::FaceSdf);
               ++value) {
            const auto semantic =
                static_cast<MaterialResourceSemantic>(value);
            if (ImGui::Selectable(ToString(semantic).data(),
                                  semantic == resource->semantic)) {
              MaterialAssetData before = source;
              resource->semantic = semantic;
              CommitMaterialMutation(std::move(before),
                                     "Change texture semantic", scene);
            }
          }
          ImGui::EndCombo();
        }
        ImGui::TextDisabled("Color Space: %s",
                            ResourceColorSpace(resource->semantic));

        if (ImGui::TreeNodeEx("Advanced Mapping",
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
        int uvSet = static_cast<int>(resource->texCoord);
        if (ImGui::Combo("UV Map", &uvSet, "UV0\0UV1\0")) {
          MaterialAssetData before = source;
          resource->texCoord = static_cast<uint32_t>(uvSet);
          CommitMaterialMutation(std::move(before),
                                 "Change texture UV set", scene);
        }
        {
          MaterialAssetData before = source;
          const bool changed = ImGui::DragFloat2(
              "UV Scale", &resource->uvScale.x, 0.01f);
          if (ImGui::IsItemActivated() && !materialGesture_) {
            materialGesture_ = MaterialEditGesture{
                std::move(before), "Edit texture UV scale"};
          }
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitMaterialGesture(scene);
          }
          if (changed) PreviewMaterialGesture(scene);
        }
        {
          MaterialAssetData before = source;
          const bool changed = ImGui::DragFloat2(
              "UV Offset", &resource->uvOffset.x, 0.01f);
          if (ImGui::IsItemActivated() && !materialGesture_) {
            materialGesture_ = MaterialEditGesture{
                std::move(before), "Edit texture UV offset"};
          }
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitMaterialGesture(scene);
          }
          if (changed) PreviewMaterialGesture(scene);
        }
        {
          MaterialAssetData before = source;
          const bool changed = ImGui::DragFloat(
              "UV Rotation", &resource->uvRotation, 0.01f);
          if (ImGui::IsItemActivated() && !materialGesture_) {
            materialGesture_ = MaterialEditGesture{
                std::move(before), "Edit texture UV rotation"};
          }
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitMaterialGesture(scene);
          }
          if (changed) PreviewMaterialGesture(scene);
        }
        ImGui::TextDisabled(
            "Sampler: engine material default | Outputs: Color, RGB, R/G/B/A");
        ImGui::TreePop();
        } else {
          ImGui::TextDisabled(
              "Color and channel outputs are available on the graph node.");
        }
      }
    } else if (node->kind == MaterialNodeKind::RampDiffuse ||
               node->kind == MaterialNodeKind::MultiRampDiffuse ||
               node->kind == MaterialNodeKind::MatCap ||
               node->kind == MaterialNodeKind::FaceSdf) {
      DrawMaterialGraphResponseResourceInspector(
          scene, source, graph, *node);
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
  } else {
    ImGui::TextDisabled("CURRENT NODE");
    ImGui::TextWrapped(
        "Select a graph node to edit only the properties it owns.");
  }

  if (!selectedGraphEdgeId_.empty()) {
    const auto selectedEdge = std::find_if(
        graph.edges.begin(), graph.edges.end(), [this](const auto &edge) {
          return edge.id == selectedGraphEdgeId_;
        });
    if (selectedEdge != graph.edges.end()) {
      ImGui::SeparatorText("Selected Connection");
      ImGui::TextWrapped("%s.%s -> %s.%s",
                         selectedEdge->sourceNodeId.c_str(),
                         selectedEdge->sourcePin.c_str(),
                         selectedEdge->targetNodeId.c_str(),
                         selectedEdge->targetPin.c_str());
    }
  }
  ImGui::BeginDisabled(selectedGraphEdgeId_.empty());
  const bool requestDeleteEdge = ImGui::Button("Delete Connection");
  ImGui::EndDisabled();

  const MaterialCompileResult &compile = authoringDocument_.GetCompileResult();
  if (ImGui::CollapsingHeader("Compile & Diagnostics")) {
    ImGui::Text("Revision %llu / %llu",
                static_cast<unsigned long long>(
                    authoringDocument_.GetMaterialRevision()),
                static_cast<unsigned long long>(
                    authoringDocument_.GetCompileRevision()));
    ImGui::TextWrapped("%s%s%s",
                       compile.success ? "Compiled" : "Compile failed",
                       compile.usedLastGood ? " | last-good" : "",
                       compile.usedFallback ? " | PBR fallback" : "");
    for (const MaterialCompileDiagnostic &diagnostic : compile.diagnostics) {
      ImGui::BulletText("%s: %s", diagnostic.code.c_str(),
                        diagnostic.message.c_str());
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
