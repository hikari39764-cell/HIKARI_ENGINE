#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

namespace {

bool AsciiEqualFold(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t index = 0u; index < lhs.size(); ++index) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z'
                ? static_cast<char>(c - 'A' + 'a')
                : c;
        };
        if (lower(lhs[index]) != lower(rhs[index])) {
            return false;
        }
    }
    return true;
}

std::string JoinPins(
    const std::vector<std::string_view>& pins,
    std::string_view fallback) {
    if (pins.empty()) {
        return std::string(fallback);
    }
    std::string result;
    for (const std::string_view pin : pins) {
        if (!result.empty()) {
            result += ", ";
        }
        result += pin;
    }
    return result;
}

std::optional<std::string> ResolveOutputPin(
    const MaterialGraphNodeData& node,
    const std::string& requested) {
    const std::vector<std::string_view> outputs =
        GetMaterialGraphNodeOutputPins(node);
    if (requested.empty() || requested == "out" ||
        requested == "output" || requested == "o") {
        return outputs.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>(
                  std::string(outputs.front()));
    }
    for (const std::string_view output : outputs) {
        if (AsciiEqualFold(output, requested)) {
            return std::string(output);
        }
    }
    return std::nullopt;
}

std::optional<std::string> ResolveInputPin(
    const MaterialGraphNodeData& node,
    const std::string& requested) {
    const std::vector<std::string_view> inputs =
        GetMaterialGraphNodeInputPins(node);
    if (requested.empty() || requested == "in" ||
        requested == "input" || requested == "i") {
        return inputs.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>(
                  std::string(inputs.front()));
    }
    for (const std::string_view input : inputs) {
        if (AsciiEqualFold(input, requested)) {
            return std::string(input);
        }
    }
    return std::nullopt;
}

std::optional<MaterialNodeKind> ResolveNodeKind(
    const std::string& input) {
    if (const MaterialNodeDefinition* definition =
            FindMaterialNodeDefinition(std::string_view(input))) {
        return definition->kind;
    }
    std::string normalized = input;
    if (normalized.rfind("node.", 0u) == 0u) {
        normalized = normalized.substr(5u);
    }
    const auto asciiLower = [](char c) {
        return c >= 'A' && c <= 'Z'
            ? static_cast<char>(c - 'A' + 'a')
            : c;
    };
    std::string lowered = normalized;
    for (char& c : lowered) {
        c = asciiLower(c);
    }
    if (lowered == "additive") {
        // Template alias "node.additive" maps to the Add math node; connect
        // validation still enforces output routing at edit time.
        return MaterialNodeKind::Add;
    }
    for (const MaterialNodeDefinition& definition :
         GetMaterialNodeDefinitions()) {
        std::string candidate(definition.stableName);
        for (char& c : candidate) {
            c = asciiLower(c);
        }
        if (candidate == lowered) {
            return definition.kind;
        }
    }
    return std::nullopt;
}

} // namespace

bool ModelMaterialWorkspaceController::ApplyAiMaterialGraphEdit(
    DocumentSceneBase &scene,
    const AiMaterialGraphEditRequest &request,
    std::string &outMessage,
    std::vector<std::string> &outDetails) {
  outDetails.clear();
  if (!authoringDocument_.HasMaterial()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData before = authoringDocument_.GetMaterial();
  MaterialAssetData &material = authoringDocument_.EditMaterial();
  MaterialGraphData *graph = material.kind == MaterialAssetKind::Material
      ? &material.graph
      : material.kind == MaterialAssetKind::Function
          ? &material.function.graph
          : nullptr;
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  const auto findNode = [graph](const std::string &key)
      -> MaterialGraphNodeData * {
    for (MaterialGraphNodeData &node : graph->nodes) {
      if (node.id == key || node.displayName == key) {
        return &node;
      }
    }
    return nullptr;
  };

  if (request.operation == "add_node") {
    const std::optional<MaterialNodeKind> resolvedKind =
        ResolveNodeKind(request.nodeType);
    if (!resolvedKind.has_value()) {
      outMessage = "未知节点类型：" + request.nodeType +
          "；可用类型（部分）：";
      size_t listed = 0u;
      for (const MaterialNodeDefinition &definition :
           GetMaterialNodeDefinitions()) {
        if (listed >= 24u) {
          break;
        }
        if (listed > 0u) {
          outMessage += " / ";
        }
        outMessage += definition.stableName;
        ++listed;
      }
      outMessage +=
          "；成品节点例如 node.additive / node.skinLighting";
      return false;
    }
    const MaterialNodeKind kind = *resolvedKind;
    const MaterialNodeDefinition *definition =
        FindMaterialNodeDefinition(kind);
    if (definition == nullptr) {
      outMessage = "节点类型未注册：" + request.nodeType;
      return false;
    }
    if (ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
        std::any_of(
            graph->nodes.begin(), graph->nodes.end(),
            [kind](const MaterialGraphNodeData &node) {
              return node.kind == kind;
            })) {
      outMessage = "该材质已经包含单例节点：" +
          std::string(definition->stableName);
      return false;
    }
    if (kind == MaterialNodeKind::SurfaceOutput &&
        !graph->outputNodeId.empty()) {
      outMessage = "材质已经有一个 Surface Output 节点";
      return false;
    }
    MaterialGraphNodeData node{};
    node.id = "Node_" + std::to_string(authoringIdCounter_++);
    node.kind = kind;
    node.displayName = request.name.empty()
        ? std::string(definition->stableName)
        : request.name;
    if (request.positionX != 0.0f || request.positionY != 0.0f) {
      node.position = { request.positionX, request.positionY };
    } else {
      // Auto-layout so AI-created nodes do not pile up at the origin.
      const size_t column = graph->nodes.size() % 6u;
      const size_t row = graph->nodes.size() / 6u;
      node.position = {
          static_cast<float>(column) * 260.0f + 40.0f,
          static_cast<float>(row) * 200.0f + 40.0f,
      };
    }
    node.valueType = MATERIAL_GRAPH_UI::DefaultNodeValueType(kind);
    node.value.type = node.valueType;
    ASSETS::MATERIAL::InitializeReadyMadeMaterialNode(material, node);
    if (kind == MaterialNodeKind::RampDiffuse ||
        kind == MaterialNodeKind::MultiRampDiffuse ||
        kind == MaterialNodeKind::FaceSdf) {
      (void)BindBuiltInRampPresets(material);
    }
    if (kind == MaterialNodeKind::Parameter &&
        !graph->parameters.empty()) {
      node.referenceId = graph->parameters.front().id;
    }
    if (kind == MaterialNodeKind::SurfaceOutput) {
      graph->outputNodeId = node.id;
    }
    const std::string nodeId = node.id;
    const std::string displayName = node.displayName;
    graph->nodes.push_back(std::move(node));
    CommitMaterialMutation(
        std::move(before), "AI add material graph node", scene);
    outMessage = "已添加节点 " + displayName + "（id=" + nodeId + "）";
    const MaterialGraphNodeData *added =
        MATERIAL_GRAPH_UI::FindNode(*graph, nodeId);
    if (added != nullptr) {
        outDetails.push_back(
            "inputs: " + JoinPins(
                GetMaterialGraphNodeInputPins(*added), "（无输入）"));
        outDetails.push_back(
            "outputs: " + JoinPins(
                GetMaterialGraphNodeOutputPins(*added), "（无输出）"));
    }
    return true;
  }

  if (request.operation == "remove_node") {
    MaterialGraphNodeData *node = findNode(request.name);
    if (node == nullptr) {
      outMessage = "找不到节点：" + request.name;
      return false;
    }
    if (node->kind == MaterialNodeKind::SurfaceOutput) {
      outMessage = "Surface Output 是材质根节点，不能删除";
      return false;
    }
    const std::string removedId = node->id;
    std::erase_if(graph->nodes, [&removedId](const auto &existing) {
      return existing.id == removedId;
    });
    std::erase_if(graph->edges, [&removedId](const auto &edge) {
      return edge.sourceNodeId == removedId ||
          edge.targetNodeId == removedId;
    });
    CommitMaterialMutation(
        std::move(before), "AI remove material graph node", scene);
    outMessage = "已删除节点：" + removedId;
    return true;
  }

  if (request.operation == "connect") {
    MaterialGraphNodeData *sourceNode = findNode(request.sourceNode);
    MaterialGraphNodeData *targetNode = findNode(request.targetNode);
    if (sourceNode == nullptr) {
      outMessage = "找不到源节点：" + request.sourceNode;
      return false;
    }
    if (targetNode == nullptr) {
      outMessage = "找不到目标节点：" + request.targetNode;
      return false;
    }
    const std::optional<std::string> resolvedSourcePin =
        ResolveOutputPin(*sourceNode, request.sourcePin);
    if (!resolvedSourcePin.has_value()) {
      outMessage = "源节点 " + sourceNode->id + " 没有可用输出，"
          "或找不到输出 pin：" + request.sourcePin;
      outDetails.push_back(
          "outputs: " + JoinPins(
              GetMaterialGraphNodeOutputPins(*sourceNode),
              "（无输出）"));
      return false;
    }
    const std::optional<std::string> resolvedTargetPin =
        ResolveInputPin(*targetNode, request.targetPin);
    if (!resolvedTargetPin.has_value()) {
      outMessage = "目标节点 " + targetNode->id + " 没有可用输入，"
          "或找不到输入 pin：" + request.targetPin;
      outDetails.push_back(
          "inputs: " + JoinPins(
              GetMaterialGraphNodeInputPins(*targetNode),
              "（无输入）"));
      return false;
    }
    std::string reason;
    if (!CanConnectMaterialGraphPins(
            *graph, sourceNode->id, *resolvedSourcePin,
            targetNode->id, *resolvedTargetPin, reason)) {
      outMessage = "无法连线：" + reason;
      outDetails.push_back(
          "源输出: " + JoinPins(
              GetMaterialGraphNodeOutputPins(*sourceNode),
              "（无输出）"));
      outDetails.push_back(
          "目标输入: " + JoinPins(
              GetMaterialGraphNodeInputPins(*targetNode),
              "（无输入）"));
      return false;
    }
    std::erase_if(
        graph->edges, [&targetNode, &resolvedTargetPin](const auto &edge) {
          return edge.targetNodeId == targetNode->id &&
              edge.targetPin == *resolvedTargetPin;
        });
    MaterialGraphEdgeData edge{};
    edge.id = "Edge_" + std::to_string(authoringIdCounter_++);
    edge.sourceNodeId = sourceNode->id;
    edge.sourcePin = *resolvedSourcePin;
    edge.targetNodeId = targetNode->id;
    edge.targetPin = *resolvedTargetPin;
    graph->edges.push_back(std::move(edge));
    CommitMaterialMutation(
        std::move(before), "AI connect material graph nodes", scene);
    outMessage = "已连接 " + sourceNode->id + ":" +
        *resolvedSourcePin + " -> " + targetNode->id + ":" +
        *resolvedTargetPin;
    return true;
  }

  if (request.operation == "disconnect") {
    const auto findEdge = [&graph, &request, &findNode]()
        -> MaterialGraphEdgeData * {
      if (!request.edgeId.empty()) {
        for (MaterialGraphEdgeData &edge : graph->edges) {
          if (edge.id == request.edgeId) {
            return &edge;
          }
        }
        return nullptr;
      }
      const MaterialGraphNodeData *sourceNode =
          findNode(request.sourceNode);
      const MaterialGraphNodeData *targetNode =
          findNode(request.targetNode);
      const std::optional<std::string> sourcePin =
          sourceNode != nullptr
              ? ResolveOutputPin(*sourceNode, request.sourcePin)
              : std::nullopt;
      const std::optional<std::string> targetPin =
          targetNode != nullptr
              ? ResolveInputPin(*targetNode, request.targetPin)
              : std::nullopt;
      for (MaterialGraphEdgeData &edge : graph->edges) {
        const bool sourceMatches =
            sourceNode != nullptr && sourcePin.has_value() &&
            edge.sourceNodeId == sourceNode->id &&
            edge.sourcePin == *sourcePin;
        const bool targetMatches =
            targetNode != nullptr && targetPin.has_value() &&
            edge.targetNodeId == targetNode->id &&
            edge.targetPin == *targetPin;
        if (sourceMatches && targetMatches) {
          return &edge;
        }
      }
      return nullptr;
    };
    MaterialGraphEdgeData *edge = findEdge();
    if (edge == nullptr) {
      outMessage = "找不到要断开的连线";
      return false;
    }
    const std::string removedId = edge->id;
    std::erase_if(graph->edges, [&removedId](const auto &existing) {
      return existing.id == removedId;
    });
    CommitMaterialMutation(
        std::move(before), "AI disconnect material graph edge", scene);
    outMessage = "已断开连线：" + removedId;
    return true;
  }

  outMessage = "未知的 graph_edit 操作：" + request.operation +
      "（支持 add_node / remove_node / connect / disconnect）";
  return false;
}

bool ModelMaterialWorkspaceController::QueryAiMaterialGraph(
    DocumentSceneBase &scene,
    const std::string &nameFilter,
    std::string &outMessage,
    std::vector<std::string> &outDetails) {
  (void)scene;
  outDetails.clear();
  if (!authoringDocument_.HasMaterial()) {
    outMessage = "未打开材质文档";
    return false;
  }
  const MaterialAssetData &material = authoringDocument_.GetMaterial();
  const MaterialGraphData *graph =
      material.kind == MaterialAssetKind::Material
          ? &material.graph
          : material.kind == MaterialAssetKind::Function
              ? &material.function.graph
              : nullptr;
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  const auto matches = [&nameFilter](const std::string &text) {
    return nameFilter.empty() ||
        text.find(nameFilter) != std::string::npos;
  };
  for (const MaterialGraphNodeData &node : graph->nodes) {
    if (!matches(node.id) && !matches(node.displayName)) {
      continue;
    }
    std::string line =
        node.id + " | " + node.displayName +
        " | kind=" + node.kind.value;
    if (node.kind == MaterialNodeKind::SurfaceOutput) {
      line += " | OUTPUT";
    }
    outDetails.push_back(line);
    outDetails.push_back(
        "  inputs: " + JoinPins(
            GetMaterialGraphNodeInputPins(node), "（无）"));
    outDetails.push_back(
        "  outputs: " + JoinPins(
            GetMaterialGraphNodeOutputPins(node), "（无）"));
    for (const MaterialGraphEdgeData &edge : graph->edges) {
      if (edge.targetNodeId == node.id) {
        outDetails.push_back(
            "  in " + edge.targetPin + " <- " +
            edge.sourceNodeId + "." + edge.sourcePin);
      }
    }
  }
  if (outDetails.empty()) {
    outMessage = "没有匹配的材质图节点";
    return false;
  }
  outMessage = "材质图查询：";
  return true;
}

} // namespace HIKARI::EDITOR
