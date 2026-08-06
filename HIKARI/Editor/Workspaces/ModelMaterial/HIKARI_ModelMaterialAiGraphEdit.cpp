#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::ApplyAiMaterialGraphEdit(
    DocumentSceneBase &scene,
    const AiMaterialGraphEditRequest &request,
    std::string &outMessage) {
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
    const MaterialNodeDefinition *definition =
        FindMaterialNodeDefinition(
            std::string_view(request.nodeType));
    if (definition == nullptr) {
      outMessage = "未知节点类型：" + request.nodeType +
          "（示例：node.additive / constant / parameter）";
      return false;
    }
    const MaterialNodeKind kind = definition->kind;
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
    node.position = { request.positionX, request.positionY };
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
    std::string reason;
    if (!CanConnectMaterialGraphPins(
            *graph, sourceNode->id, request.sourcePin,
            targetNode->id, request.targetPin, reason)) {
      outMessage = "无法连线：" + reason;
      return false;
    }
    std::erase_if(
        graph->edges, [&targetNode, &request](const auto &edge) {
          return edge.targetNodeId == targetNode->id &&
              edge.targetPin == request.targetPin;
        });
    MaterialGraphEdgeData edge{};
    edge.id = "Edge_" + std::to_string(authoringIdCounter_++);
    edge.sourceNodeId = sourceNode->id;
    edge.sourcePin = request.sourcePin;
    edge.targetNodeId = targetNode->id;
    edge.targetPin = request.targetPin;
    graph->edges.push_back(std::move(edge));
    CommitMaterialMutation(
        std::move(before), "AI connect material graph nodes", scene);
    outMessage = "已连接 " + sourceNode->id + ":" +
        request.sourcePin + " -> " + targetNode->id + ":" +
        request.targetPin;
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
      for (MaterialGraphEdgeData &edge : graph->edges) {
        const bool sourceMatches =
            sourceNode != nullptr &&
            edge.sourceNodeId == sourceNode->id &&
            edge.sourcePin == request.sourcePin;
        const bool targetMatches =
            targetNode != nullptr &&
            edge.targetNodeId == targetNode->id &&
            edge.targetPin == request.targetPin;
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

} // namespace HIKARI::EDITOR
