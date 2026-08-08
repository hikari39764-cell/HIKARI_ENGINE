#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

namespace HIKARI::EDITOR {

namespace {

size_t NodeCategoryColumn(MaterialNodeKind kind) {
  const MaterialNodeDefinition *definition =
      FindMaterialNodeDefinition(kind);
  if (definition == nullptr) {
    return 2u;
  }
  switch (definition->semanticClass) {
  case MaterialNodeSemanticClass::Input:
    return 0u;
  case MaterialNodeSemanticClass::Math:
    return 1u;
  case MaterialNodeSemanticClass::Shading:
    return 2u;
  case MaterialNodeSemanticClass::Surface:
    return 3u;
  case MaterialNodeSemanticClass::Function:
    return 2u;
  }
  return 2u;
}

size_t NodeCategoryColumn(const MaterialNodeTypeId& typeId) {
  MaterialNodeKind kind{};
  return TryGetLegacyMaterialNodeKind(typeId, kind)
      ? NodeCategoryColumn(kind)
      : 2u;
}

} // namespace

bool ModelMaterialWorkspaceController::IsGraphNodeSelected(
    const std::string &id) const {
  return std::find(
      selectedGraphNodeIds_.begin(),
      selectedGraphNodeIds_.end(),
      id) != selectedGraphNodeIds_.end();
}

void ModelMaterialWorkspaceController::SetGraphNodeSelection(
    const std::string &id, bool additive) {
  if (!additive) {
    selectedGraphNodeIds_.clear();
  }
  const auto found = std::find(
      selectedGraphNodeIds_.begin(),
      selectedGraphNodeIds_.end(),
      id);
  if (additive && found != selectedGraphNodeIds_.end()) {
    selectedGraphNodeIds_.erase(found);
  } else if (found == selectedGraphNodeIds_.end()) {
    selectedGraphNodeIds_.push_back(id);
  }
  selectedGraphNodeId_ = id;
  selectedGraphEdgeId_.clear();
}

void ModelMaterialWorkspaceController::ClearGraphNodeSelection() {
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
}

bool ModelMaterialWorkspaceController::CopySelectedGraphNodes(
    MaterialAssetData &source, MaterialGraphData &graph) {
  (void)source;
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  graphClipboard_ = {};
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  for (const MaterialGraphNodeData &node : graph.nodes) {
    if (selected.contains(node.id)) {
      graphClipboard_.nodes.push_back(node);
    }
  }
  if (graphClipboard_.nodes.empty()) {
    return false;
  }
  std::unordered_set<std::string> parameterIds{};
  std::unordered_set<std::string> resourceIds{};
  for (const MaterialGraphNodeData &node : graphClipboard_.nodes) {
    if (node.kind == MaterialNodeKind::Parameter &&
        !node.referenceId.empty()) {
      parameterIds.insert(node.referenceId);
    }
    if (node.kind == MaterialNodeKind::TextureSample &&
        !node.referenceId.empty()) {
      resourceIds.insert(node.referenceId);
    }
  }
  for (const MaterialGraphParameterData &parameter :
       graph.parameters) {
    if (parameterIds.contains(parameter.id)) {
      graphClipboard_.parameters.push_back(parameter);
    }
  }
  for (const MaterialGraphResourceData &resource :
       graph.resources) {
    if (resourceIds.contains(resource.id)) {
      graphClipboard_.resources.push_back(resource);
    }
  }
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    if (selected.contains(edge.sourceNodeId) &&
        selected.contains(edge.targetNodeId)) {
      graphClipboard_.edges.push_back(edge);
    }
  }
  return true;
}

bool ModelMaterialWorkspaceController::PasteGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph,
    const MATH::Vec2 &offset) {
  if (graphClipboard_.nodes.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  std::unordered_map<std::string, std::string> nodeMap{};
  std::unordered_map<std::string, std::string> parameterMap{};
  std::unordered_map<std::string, std::string> resourceMap{};
  std::vector<std::string> pastedIds{};
  for (const MaterialGraphNodeData &clip : graphClipboard_.nodes) {
    MaterialGraphNodeData node = clip;
    const std::string oldId = node.id;
    node.id = "Node_" + std::to_string(authoringIdCounter_++);
    node.position.x += offset.x;
    node.position.y += offset.y;
    if (node.kind == MaterialNodeKind::Parameter &&
        !node.referenceId.empty()) {
      const std::string oldParameterId = node.referenceId;
      const std::string newParameterId =
          "parameter.paste_" + node.id;
      parameterMap[oldParameterId] = newParameterId;
      const auto sourceParameter = std::find_if(
          graphClipboard_.parameters.begin(),
          graphClipboard_.parameters.end(),
          [&oldParameterId](const MaterialGraphParameterData &p) {
            return p.id == oldParameterId;
          });
      if (sourceParameter != graphClipboard_.parameters.end()) {
        MaterialGraphParameterData parameter = *sourceParameter;
        parameter.id = newParameterId;
        graph.parameters.push_back(std::move(parameter));
      }
      node.referenceId = newParameterId;
    }
    if (node.kind == MaterialNodeKind::TextureSample &&
        !node.referenceId.empty()) {
      const std::string oldResourceId = node.referenceId;
      const std::string newResourceId =
          "Texture_" + std::to_string(authoringIdCounter_++);
      resourceMap[oldResourceId] = newResourceId;
      const auto sourceResource = std::find_if(
          graphClipboard_.resources.begin(),
          graphClipboard_.resources.end(),
          [&oldResourceId](const MaterialGraphResourceData &r) {
            return r.id == oldResourceId;
          });
      if (sourceResource != graphClipboard_.resources.end()) {
        MaterialGraphResourceData resource = *sourceResource;
        resource.id = newResourceId;
        graph.resources.push_back(std::move(resource));
      }
      node.referenceId = newResourceId;
    }
    nodeMap[oldId] = node.id;
    pastedIds.push_back(node.id);
    graph.nodes.push_back(std::move(node));
  }
  for (const MaterialGraphEdgeData &clip : graphClipboard_.edges) {
    MaterialGraphEdgeData edge = clip;
    const auto sourceIt = nodeMap.find(edge.sourceNodeId);
    const auto targetIt = nodeMap.find(edge.targetNodeId);
    if (sourceIt == nodeMap.end() || targetIt == nodeMap.end()) {
      continue;
    }
    edge.id = "Edge_" + std::to_string(authoringIdCounter_++);
    edge.sourceNodeId = sourceIt->second;
    edge.targetNodeId = targetIt->second;
    graph.edges.push_back(std::move(edge));
  }
  selectedGraphNodeIds_ = std::move(pastedIds);
  selectedGraphNodeId_ = selectedGraphNodeIds_.empty()
      ? std::string{}
      : selectedGraphNodeIds_.back();
  selectedGraphEdgeId_.clear();
  CommitMaterialMutation(
      std::move(before), "Paste material graph nodes", scene);
  return true;
}

bool ModelMaterialWorkspaceController::DuplicateSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (!CopySelectedGraphNodes(source, graph)) {
    return false;
  }
  return PasteGraphNodes(
      scene, source, graph, MATH::Vec2{ 30.0f, 30.0f });
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  const bool hasDeletableNode = std::any_of(
      graph.nodes.begin(), graph.nodes.end(),
      [&selected](const MaterialGraphNodeData &node) {
        return selected.contains(node.id) &&
            node.kind != MaterialNodeKind::SurfaceOutput;
      });
  if (!hasDeletableNode) {
    return false;
  }
  std::erase_if(
      graph.nodes, [&selected](const MaterialGraphNodeData &node) {
        return selected.contains(node.id) &&
            node.kind != MaterialNodeKind::SurfaceOutput;
      });
  std::erase_if(
      graph.edges, [&selected](const MaterialGraphEdgeData &edge) {
        return selected.contains(edge.sourceNodeId) ||
            selected.contains(edge.targetNodeId);
      });
  CommitMaterialMutation(
      std::move(before), "Delete material graph nodes", scene);
  for (const std::string &id : selected) {
    collapsedGraphNodeIds_.erase(id);
  }
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
  selectedGraphEdgeId_.clear();
  return true;
}

bool ModelMaterialWorkspaceController::ArrangeSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  std::vector<MaterialGraphNodeData *> selected{};
  for (MaterialGraphNodeData &node : graph.nodes) {
    if (IsGraphNodeSelected(node.id)) {
      selected.push_back(&node);
    }
  }
  std::sort(
      selected.begin(), selected.end(),
      [](const MaterialGraphNodeData *lhs,
         const MaterialGraphNodeData *rhs) {
        return lhs->displayName < rhs->displayName;
      });
  constexpr float kColumnSpacing = 260.0f;
  constexpr float kRowSpacing = 220.0f;
  size_t column = 0u;
  size_t row = 0u;
  for (MaterialGraphNodeData *node : selected) {
    node->position = {
      40.0f + static_cast<float>(column) * kColumnSpacing,
      40.0f + static_cast<float>(row) * kRowSpacing,
    };
    ++column;
    if (column >= 4u) {
      column = 0u;
      ++row;
    }
  }
  CommitMaterialMutation(
      std::move(before), "Arrange selected material graph nodes", scene);
  return true;
}

bool ModelMaterialWorkspaceController::CreateGraphGroupFromSelection(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  float minX = (std::numeric_limits<float>::max)();
  float minY = (std::numeric_limits<float>::max)();
  float maxX = -(std::numeric_limits<float>::max)();
  float maxY = -(std::numeric_limits<float>::max)();
  bool hasNode = false;
  for (const MaterialGraphNodeData &node : graph.nodes) {
    if (!selected.contains(node.id)) {
      continue;
    }
    minX = (std::min)(minX, node.position.x);
    minY = (std::min)(minY, node.position.y);
    maxX = (std::max)(
        maxX, node.position.x + MATERIAL_GRAPH_UI::kNodeWidth);
    maxY = (std::max)(maxY, node.position.y + 120.0f);
    hasNode = true;
  }
  if (!hasNode) {
    return false;
  }
  const uint64_t groupNumber = authoringIdCounter_++;
  MaterialGraphGroupData group{};
  group.id = "Group_" + std::to_string(groupNumber);
  group.name = "Group " + std::to_string(groupNumber);
  group.position = { minX - 20.0f, minY - 40.0f };
  group.size = {
      (maxX - minX) + 40.0f,
      (maxY - minY) + 40.0f };
  group.nodeIds.assign(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  graph.groups.push_back(std::move(group));
  CommitMaterialMutation(
      std::move(before), "Create material graph group", scene);
  return true;
}

bool ModelMaterialWorkspaceController::UngroupSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  std::erase_if(
      graph.groups, [&selected](const MaterialGraphGroupData &group) {
        return std::any_of(
            group.nodeIds.begin(), group.nodeIds.end(),
            [&selected](const std::string &nodeId) {
              return selected.contains(nodeId);
            });
      });
  CommitMaterialMutation(
      std::move(before), "Ungroup material graph nodes", scene);
  return true;
}

void ModelMaterialWorkspaceController::SetOperationRegistry(
    const EditorOperationRegistry *registry) noexcept {
  operationRegistry_ = registry;
}

bool ModelMaterialWorkspaceController::HasMaterialDocument()
    const noexcept {
  return authoringDocument_.HasMaterial();
}

bool ModelMaterialWorkspaceController::AddMaterialGraphNode(
    DocumentSceneBase &scene,
    const MaterialGraphNodeCreateRequest &request,
    std::string &outMessage,
    std::vector<std::string> &outDetails,
    std::string &outCode) {
  std::lock_guard<std::recursive_mutex> guard(
      authoringDocument_.GetMutex());
  outDetails.clear();
  outCode = "OK";
  if (!authoringDocument_.HasMaterial()) {
    outMessage = "未打开材质文档";
    outCode = "MATERIAL_DOCUMENT_MISSING";
    return false;
  }
  MaterialAssetData &material = authoringDocument_.EditMaterial();
  MaterialGraphData *graph = material.kind == MaterialAssetKind::Material
      ? &material.graph
      : material.kind == MaterialAssetKind::Function
          ? &material.function.graph
          : nullptr;
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    outCode = "NO_GRAPH";
    return false;
  }
  const std::optional<MaterialNodeKind> resolvedKind =
      ResolveMaterialNodeKind(request.nodeType);
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
    outCode = "UNKNOWN_NODE_TYPE";
    return false;
  }
  const MaterialNodeKind kind = *resolvedKind;
  if (kind == MaterialNodeKind::FunctionCall) {
    if (!request.allowFunctionCall ||
        !request.functionAssetGuid.IsValid()) {
      outMessage =
          "Material Function 节点必须绑定函数资产 GUID；"
          "AI 暂不能创建，请在编辑器的 Material Functions 菜单添加。";
      outCode = "FUNCTION_CALL_UNSUPPORTED";
      return false;
    }
  }
  const MaterialNodeDefinition *definition =
      FindMaterialNodeDefinition(kind);
  if (definition == nullptr) {
    outMessage = "节点类型未注册：" + request.nodeType;
    outCode = "NODE_TYPE_UNREGISTERED";
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
    outCode = "SINGLETON_NODE_EXISTS";
    return false;
  }
  if (kind == MaterialNodeKind::SurfaceOutput &&
      !graph->outputNodeId.empty()) {
    outMessage = "材质已经有一个 Surface Output 节点";
    outCode = "SURFACE_OUTPUT_EXISTS";
    return false;
  }
  MaterialAssetData before = material;
  MaterialGraphNodeData node{};
  node.id = "Node_" + std::to_string(authoringIdCounter_++);
  node.kind = kind;
  node.displayName = request.displayName.empty()
      ? std::string(definition->stableName)
      : request.displayName;
  if (request.manualPosition) {
    node.position = request.position;
  } else {
    // Auto-layout by category so newly created nodes do not pile up.
    const size_t column = NodeCategoryColumn(kind);
    size_t row = 0u;
    for (const MaterialGraphNodeData &existing : graph->nodes) {
      if (NodeCategoryColumn(existing.kind) == column) {
        ++row;
      }
    }
    node.position = {
        static_cast<float>(column) * 300.0f + 40.0f,
        static_cast<float>(row) * 240.0f + 40.0f,
    };
  }
  if (kind == MaterialNodeKind::FunctionCall) {
    node.assetReference = request.functionAssetGuid;
    node.functionInputs = request.functionInputs;
    node.functionOutputs = request.functionOutputs;
    node.valueType = node.functionOutputs.empty()
        ? MATERIAL_GRAPH_UI::DefaultNodeValueType(kind)
        : node.functionOutputs.front().defaultValue.type;
    node.value.type = node.valueType;
  } else {
    if (!request.valueType.empty()) {
      const std::optional<MaterialValueType> parsedType =
          MATERIAL_GRAPH_UI::ParseMaterialValueType(request.valueType);
      if (!parsedType.has_value()) {
        outMessage = "未知 valueType：" + request.valueType +
            "（支持 Float1/Float2/Float3/Float4/Color）";
        outCode = "INVALID_VALUE_TYPE";
        return false;
      }
      if (kind != MaterialNodeKind::Constant &&
          kind != MaterialNodeKind::Parameter) {
        outMessage = "valueType 仅支持 Constant/Parameter 节点";
        outCode = "VALUE_TYPE_UNSUPPORTED";
        return false;
      }
      node.valueType = *parsedType;
      node.value.type = *parsedType;
    } else {
      node.valueType = MATERIAL_GRAPH_UI::DefaultNodeValueType(kind);
      node.value.type = node.valueType;
    }
    ASSETS::MATERIAL::InitializeReadyMadeMaterialNode(material, node);
    if ((kind == MaterialNodeKind::Constant ||
         kind == MaterialNodeKind::Parameter) &&
        request.valueSet) {
      node.value.numeric = {
          request.valueX, request.valueY,
          request.valueZ, request.valueW };
    }
    if (kind == MaterialNodeKind::RampDiffuse ||
        kind == MaterialNodeKind::MultiRampDiffuse ||
        kind == MaterialNodeKind::FaceSdf) {
      (void)BindBuiltInRampPresets(material);
    }
    if (kind == MaterialNodeKind::Parameter) {
      std::string parameterId = request.parameterIdHint;
      if (parameterId.empty()) {
        const std::string requestedId = request.displayName.empty()
            ? std::string{}
            : (request.displayName.rfind("parameter.", 0u) == 0u
                   ? request.displayName
                   : "parameter." + request.displayName);
        parameterId = requestedId.empty()
            ? (request.parameterIdPrefix.empty()
                   ? "parameter.ai_"
                   : request.parameterIdPrefix) +
                  node.id
            : requestedId;
      }
      const auto existing = std::find_if(
          graph->parameters.begin(), graph->parameters.end(),
          [&parameterId](const MaterialGraphParameterData &parameter) {
            return !parameterId.empty() &&
                parameter.id == parameterId;
          });
      if (existing != graph->parameters.end()) {
        node.referenceId = existing->id;
      } else {
        // Create a dedicated parameter so nodes never share the first
        // (often baseColor) parameter by accident.
        MaterialGraphParameterData parameter{};
        parameter.id = parameterId;
        parameter.displayName =
            !request.parameterDisplayName.empty()
                ? request.parameterDisplayName
                : (request.parameterIdHint.empty()
                       ? (request.displayName.empty()
                              ? "AI Parameter"
                              : request.displayName)
                       : "Parameter");
        parameter.defaultValue.type = node.valueType;
        if (request.valueSet) {
          parameter.defaultValue.numeric = {
              request.valueX, request.valueY,
              request.valueZ, request.valueW };
        }
        parameter.exposed = true;
        graph->parameters.push_back(std::move(parameter));
        node.referenceId = parameterId;
      }
    }
  }
  if (kind == MaterialNodeKind::SurfaceOutput) {
    graph->outputNodeId = node.id;
  }
  selectedGraphNodeId_ = node.id;
  const std::string nodeId = node.id;
  const std::string displayName = node.displayName;
  graph->nodes.push_back(std::move(node));
  CommitMaterialMutation(
      std::move(before), "Add material graph node", scene);
  outMessage = "已添加节点 " + displayName + "（id=" + nodeId + "）";
  const MaterialGraphNodeData *added =
      MATERIAL_GRAPH_UI::FindNode(*graph, nodeId);
  if (added != nullptr) {
    outDetails.push_back(
        "inputs: " + MATERIAL_GRAPH_UI::JoinPins(
            GetMaterialGraphNodeInputPins(*added), "（无输入）"));
    outDetails.push_back(
        "outputs: " + MATERIAL_GRAPH_UI::JoinPins(
            GetMaterialGraphNodeOutputPins(*added), "（无输出）"));
  }
  return true;
}

bool ModelMaterialWorkspaceController::DuplicateSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  return DuplicateSelectedGraphNodes(scene, source, *graph);
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!DeleteSelectedGraphNodes(scene, source, *graph)) {
    outMessage = "没有可删除的选中节点（Surface Output 不能删除）";
    return false;
  }
  outMessage = "已删除选中节点";
  return true;
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphEdge(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument() || selectedGraphEdgeId_.empty()) {
    outMessage = "没有选中的连线";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  MaterialAssetData before = source;
  const std::string deletedId = selectedGraphEdgeId_;
  std::erase_if(
      graph->edges, [&deletedId](const MaterialGraphEdgeData &edge) {
        return edge.id == deletedId;
      });
  selectedGraphEdgeId_.clear();
  CommitMaterialMutation(
      std::move(before), "Delete material graph edge", scene);
  outMessage = "已删除选中连线";
  return true;
}

bool ModelMaterialWorkspaceController::CreateGraphGroupFromSelection(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!CreateGraphGroupFromSelection(scene, source, *graph)) {
    outMessage = "没有可分组的选中节点";
    return false;
  }
  outMessage = "已创建节点分组";
  return true;
}

bool ModelMaterialWorkspaceController::UngroupSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!UngroupSelectedGraphNodes(scene, source, *graph)) {
    outMessage = "没有可取消分组的选中节点";
    return false;
  }
  outMessage = "已取消选中节点分组";
  return true;
}

bool ModelMaterialWorkspaceController::ArrangeSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!ArrangeSelectedGraphNodes(scene, source, *graph)) {
    outMessage = "没有可整理的选中节点";
    return false;
  }
  outMessage = "已整理选中节点布局";
  return true;
}

void ModelMaterialWorkspaceController::FrameMaterialGraph(
    bool selection, const MATH::Vec2 &viewportSize) noexcept {
  if (!authoringDocument_.HasMaterial()) {
    return;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr || graph->nodes.empty()) {
    return;
  }
  if (selection) {
    std::string targetId = selectedGraphNodeId_;
    if (targetId.empty() && !selectedGraphNodeIds_.empty()) {
      targetId = selectedGraphNodeIds_.front();
    }
    if (const MaterialGraphNodeData *node =
            MATERIAL_GRAPH_UI::FindNode(*graph, targetId)) {
      graphCanvas_.zoom = (std::max)(graphCanvas_.zoom, 1.0f);
      const float nodeHeight = MATERIAL_GRAPH_UI::NodeLogicalHeight(
          *graph, *node, collapsedGraphNodeIds_.contains(node->id));
      graphCanvas_.pan.x = viewportSize.x * 0.5f -
          (node->position.x + MATERIAL_GRAPH_UI::kNodeWidth * 0.5f) *
              graphCanvas_.zoom;
      graphCanvas_.pan.y = viewportSize.y * 0.5f -
          (node->position.y + nodeHeight * 0.5f) * graphCanvas_.zoom;
    }
    return;
  }
  float minX = graph->nodes.front().position.x;
  float minY = graph->nodes.front().position.y;
  float maxX = minX + MATERIAL_GRAPH_UI::kNodeWidth;
  float maxY = minY + 120.0f;
  for (const MaterialGraphNodeData &node : graph->nodes) {
    const float nodeHeight = MATERIAL_GRAPH_UI::NodeLogicalHeight(
        *graph, node, collapsedGraphNodeIds_.contains(node.id));
    minX = (std::min)(minX, node.position.x);
    minY = (std::min)(minY, node.position.y);
    maxX = (std::max)(
        maxX, node.position.x + MATERIAL_GRAPH_UI::kNodeWidth);
    maxY = (std::max)(maxY, node.position.y + nodeHeight);
  }
  NodeGraphCanvas::FrameBounds(
      graphCanvas_, viewportSize, { minX, minY }, { maxX, maxY },
      80.0f, MATERIAL_GRAPH_UI::kCanvasMinimumZoom,
      MATERIAL_GRAPH_UI::kCanvasMaximumZoom);
}

void ModelMaterialWorkspaceController::SetSelectedGraphNodesCollapsed(
    bool collapsed) noexcept {
  if (!selectedGraphNodeIds_.empty()) {
    for (const std::string &id : selectedGraphNodeIds_) {
      if (collapsed) {
        collapsedGraphNodeIds_.insert(id);
      } else {
        collapsedGraphNodeIds_.erase(id);
      }
    }
  } else if (!selectedGraphNodeId_.empty()) {
    if (collapsed) {
      collapsedGraphNodeIds_.insert(selectedGraphNodeId_);
    } else {
      collapsedGraphNodeIds_.erase(selectedGraphNodeId_);
    }
  }
}

void ModelMaterialWorkspaceController::SetAllGraphNodesCollapsed(
    bool collapsed) noexcept {
  if (!authoringDocument_.HasMaterial()) {
    return;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    return;
  }
  if (collapsed) {
    for (const MaterialGraphNodeData &node : graph->nodes) {
      collapsedGraphNodeIds_.insert(node.id);
    }
  } else {
    collapsedGraphNodeIds_.clear();
  }
}

} // namespace HIKARI::EDITOR
