#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <string_view>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawMaterialGraphNodePalette(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, MATH::Vec2 position) {
#if defined(HIKARI_WITH_EDITOR)
  (void)source;
  constexpr std::array<std::string_view, 4u> categories{
      "Ready-made Shading", "Surface", "Advanced / Inputs",
      "Advanced / Math & Masks"};
  constexpr std::string_view otherCategory = "Other";
  constexpr std::array<MaterialNodeKind, 8u> commonKinds{
      MaterialNodeKind::Constant,
      MaterialNodeKind::Parameter,
      MaterialNodeKind::Add,
      MaterialNodeKind::Multiply,
      MaterialNodeKind::Lerp,
      MaterialNodeKind::Clamp,
      MaterialNodeKind::TextureSample,
      MaterialNodeKind::TextureCoordinate,
  };

  static char searchBuffer[128] = {};
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText(
      "##MaterialGraphNodeSearch", searchBuffer,
      sizeof(searchBuffer));
  const std::string searchText = searchBuffer;
  const bool searching = !searchText.empty();
  const auto matchesSearch = [&searchText](std::string_view text) {
    if (searchText.empty()) {
      return true;
    }
    std::string folded(text);
    std::string query = searchText;
    const auto lower = [](char c) {
      return c >= 'A' && c <= 'Z'
          ? static_cast<char>(c - 'A' + 'a')
          : c;
    };
    for (char &c : folded) {
      c = lower(c);
    }
    for (char &c : query) {
      c = lower(c);
    }
    return folded.find(query) != std::string::npos;
  };

  const auto addNodeByKind =
      [&](MaterialNodeKind kind, std::string_view label,
          MaterialValueType valueType = MaterialValueType::Float1,
          bool typed = false) {
        const MaterialNodeDefinition *definition =
            FindMaterialNodeDefinition(kind);
        if (definition == nullptr) {
          return;
        }
        MaterialGraphNodeCreateRequest request{};
        request.nodeType = definition->stableName;
        request.displayName = std::string(label);
        request.position = position;
        request.manualPosition = true;
        request.parameterIdPrefix = "parameter.ui_";
        request.parameterDisplayName = "Parameter";
        if (typed) {
          request.valueSet = true;
          request.valueX = 1.0f;
          request.valueY = 1.0f;
          request.valueZ = 1.0f;
          request.valueW = 1.0f;
          switch (valueType) {
          case MaterialValueType::Float1:
            request.valueType = "Float1";
            break;
          case MaterialValueType::Float2:
            request.valueType = "Float2";
            break;
          case MaterialValueType::Float3:
            request.valueType = "Float3";
            break;
          case MaterialValueType::Float4:
            request.valueType = "Float4";
            break;
          }
        }
        std::string message;
        std::string code;
        std::vector<std::string> details;
        (void)AddMaterialGraphNode(
            scene, request, message, details, code);
      };

  if (searching) {
    ImGui::SeparatorText("搜索结果");
    size_t shown = 0u;
    for (const MaterialNodeDefinition &definition :
         GetMaterialNodeDefinitions()) {
      if (!definition.paletteVisible) {
        continue;
      }
      const std::string_view label =
          ASSETS::MATERIAL::IsReadyMadeMaterialNode(definition.kind)
              ? ASSETS::MATERIAL::ReadyMadeMaterialNodeDisplayName(
                    definition.kind)
              : definition.stableName;
      if (!matchesSearch(label) &&
          !matchesSearch(definition.stableName)) {
        continue;
      }
      if (ImGui::MenuItem(label.data())) {
        addNodeByKind(definition.kind, label);
      }
      ++shown;
      if (shown >= 40u) {
        ImGui::TextDisabled("...（更多结果请输入更精确的关键词）");
        break;
      }
    }
    if (shown == 0u) {
      ImGui::TextDisabled("没有匹配的节点");
    }
    return;
  }

  if (!searching && ImGui::BeginMenu("Common")) {
    for (const MaterialNodeKind kind : commonKinds) {
      const MaterialNodeDefinition *definition =
          FindMaterialNodeDefinition(kind);
      if (definition == nullptr || !definition->paletteVisible) {
        continue;
      }
      const bool duplicateSingleton =
          ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
          std::any_of(
              graph.nodes.begin(), graph.nodes.end(),
              [kind](const auto &node) {
                return node.kind == kind;
              });
      if (kind == MaterialNodeKind::Constant ||
          kind == MaterialNodeKind::Parameter) {
        if (ImGui::BeginMenu(definition->stableName.data())) {
          constexpr std::array<MaterialValueType, 4u> types{
              MaterialValueType::Float1,
              MaterialValueType::Float2,
              MaterialValueType::Float3,
              MaterialValueType::Float4,
          };
          constexpr std::array<const char*, 4u> typeNames{
              "Float1 (1)", "Float2 (1,1)",
              "Float3 (1,1,1)", "Float4 (1,1,1,1)",
          };
          for (size_t index = 0u; index < types.size(); ++index) {
            if (ImGui::MenuItem(typeNames[index])) {
              addNodeByKind(
                  kind,
                  std::string(definition->stableName) +
                      " (" + typeNames[index] + ")",
                  types[index], true);
            }
          }
          ImGui::EndMenu();
        }
        continue;
      }
      ImGui::BeginDisabled(duplicateSingleton);
      if (ImGui::MenuItem(definition->stableName.data())) {
        addNodeByKind(kind, definition->stableName);
      }
      ImGui::EndDisabled();
    }
    ImGui::EndMenu();
  }

  const auto mathSubgroup =
      [](MaterialNodeKind kind) -> std::string_view {
    switch (kind) {
    case MaterialNodeKind::Add:
    case MaterialNodeKind::Subtract:
    case MaterialNodeKind::Multiply:
    case MaterialNodeKind::Divide:
    case MaterialNodeKind::Minimum:
    case MaterialNodeKind::Maximum:
    case MaterialNodeKind::Absolute:
    case MaterialNodeKind::Negate:
    case MaterialNodeKind::Modulo:
    case MaterialNodeKind::Power:
    case MaterialNodeKind::SquareRoot:
      return "基础运算";
    case MaterialNodeKind::Sine:
    case MaterialNodeKind::Cosine:
    case MaterialNodeKind::Atan2:
      return "三角";
    case MaterialNodeKind::Exp:
    case MaterialNodeKind::Log2:
      return "指数/对数";
    case MaterialNodeKind::Floor:
    case MaterialNodeKind::Ceil:
    case MaterialNodeKind::Fraction:
    case MaterialNodeKind::Round:
    case MaterialNodeKind::Trunc:
    case MaterialNodeKind::Sign:
    case MaterialNodeKind::Saturate:
    case MaterialNodeKind::OneMinus:
    case MaterialNodeKind::Clamp:
      return "取整/范围";
    case MaterialNodeKind::Step:
    case MaterialNodeKind::SmoothStep:
    case MaterialNodeKind::Remap:
      return "比较/插值";
    case MaterialNodeKind::Dot:
    case MaterialNodeKind::Cross:
    case MaterialNodeKind::Length:
    case MaterialNodeKind::Normalize:
    case MaterialNodeKind::Distance:
    case MaterialNodeKind::Reflect:
    case MaterialNodeKind::MakeFloat2:
    case MaterialNodeKind::MakeFloat3:
    case MaterialNodeKind::MakeFloat4:
    case MaterialNodeKind::Split:
    case MaterialNodeKind::Select:
      return "向量";
    case MaterialNodeKind::ValueNoise:
    case MaterialNodeKind::Voronoi:
      return "噪声";
    case MaterialNodeKind::UvTransform:
      return "UV 变换";
    case MaterialNodeKind::Fresnel:
      return "着色辅助";
    default:
      return {};
    }
  };
  const auto drawNodeItem =
      [&](const MaterialNodeDefinition &definition) {
        const MaterialNodeKind kind = definition.kind;
        const bool duplicateSingleton =
            ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
            std::any_of(
                graph.nodes.begin(), graph.nodes.end(),
                [kind](const auto &node) {
                  return node.kind == kind;
                });
        ImGui::BeginDisabled(duplicateSingleton);
        const std::string_view label =
            ASSETS::MATERIAL::IsReadyMadeMaterialNode(kind)
                ? ASSETS::MATERIAL::ReadyMadeMaterialNodeDisplayName(kind)
                : definition.stableName;
        if (searching && !matchesSearch(label) &&
            !matchesSearch(definition.stableName)) {
          ImGui::EndDisabled();
          return;
        }
        if (ImGui::MenuItem(label.data())) {
          addNodeByKind(kind, label);
        }
        ImGui::EndDisabled();
      };
  const auto drawCategory =
      [&](const std::string_view category) {
        if (!ImGui::BeginMenu(category.data())) {
          return;
        }
        if (category == "Advanced / Math & Masks") {
          std::map<std::string_view,
                   std::vector<const MaterialNodeDefinition *>>
              groups;
          for (const MaterialNodeDefinition &definition :
               GetMaterialNodeDefinitions()) {
            if (!definition.paletteVisible ||
                MATERIAL_GRAPH_UI::NodeCategory(definition.kind) !=
                    category) {
              continue;
            }
            groups[mathSubgroup(definition.kind)].push_back(
                &definition);
          }
          for (const auto &[group, definitions] : groups) {
            const bool nested =
                !group.empty() && definitions.size() > 1u;
            if (nested &&
                !ImGui::BeginMenu(group.data())) {
              continue;
            }
            for (const MaterialNodeDefinition *definition :
                 definitions) {
              drawNodeItem(*definition);
            }
            if (nested) {
              ImGui::EndMenu();
            }
          }
        } else {
          for (const MaterialNodeDefinition &definition :
               GetMaterialNodeDefinitions()) {
            if (!definition.paletteVisible ||
                MATERIAL_GRAPH_UI::NodeCategory(definition.kind) !=
                    category) {
              continue;
            }
            drawNodeItem(definition);
          }
        }
        ImGui::EndMenu();
      };
  for (const std::string_view category : categories) {
    drawCategory(category);
  }
  drawCategory(otherCategory);

  if (!searching && ImGui::BeginMenu("Material Functions")) {
    bool foundFunction = false;
    for (const AssetChoice &choice : materialChoices_) {
      if (!choice.ready ||
          choice.materialKind != MaterialAssetKind::Function ||
          choice.functionOutputs.empty()) {
        continue;
      }
      foundFunction = true;
      if (!ImGui::MenuItem(choice.label.c_str())) {
        continue;
      }
      MaterialGraphNodeCreateRequest request{};
      request.nodeType = "FunctionCall";
      request.displayName = choice.label;
      request.position = position;
      request.manualPosition = true;
      request.allowFunctionCall = true;
      request.functionAssetGuid = choice.guid;
      request.functionInputs = choice.functionInputs;
      request.functionOutputs = choice.functionOutputs;
      std::string message;
      std::string code;
      std::vector<std::string> details;
      (void)AddMaterialGraphNode(
          scene, request, message, details, code);
    }
    if (!foundFunction) {
      ImGui::BeginDisabled();
      (void)ImGui::MenuItem("No imported Material Functions");
      ImGui::EndDisabled();
    }
    ImGui::EndMenu();
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)position;
#endif
}

} // namespace HIKARI::EDITOR
