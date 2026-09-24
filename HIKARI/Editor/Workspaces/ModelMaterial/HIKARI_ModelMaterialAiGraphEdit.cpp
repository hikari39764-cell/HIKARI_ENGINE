#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Core/Text/HIKARI_AsciiId.h"
#include "Assets/Material/Serialization/HIKARI_MaterialGraphJson.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

namespace {

std::optional<std::string> ResolvePin(const MaterialGraphNodeData& node, std::string_view requested, bool output) {
    const auto pins = output ? GetMaterialGraphNodeOutputPins(node) : GetMaterialGraphNodeInputPins(node);
    if (requested.empty() || (output ? requested == "out" || requested == "output" || requested == "o"
                                     : requested == "in" || requested == "input" || requested == "i"))
        return pins.empty() ? std::nullopt : std::optional<std::string>(pins.front());
    for (const auto pin : pins)
        if (TEXT::AsciiEqualFold(pin, requested))
            return std::string(pin);
    return std::nullopt;
}

void ApplyRequestedValueComponents(MaterialValueData& value, const AiMaterialGraphEditRequest& request) noexcept {
    if (!request.valueSet) {
        return;
    }
    value.numeric = {request.valueX, request.valueY, request.valueZ, request.valueW};
}

} // namespace

bool ModelMaterialWorkspaceController::ApplyAiMaterialGraphEdit(DocumentSceneBase& scene,
                                                                const AiMaterialGraphEditRequest& request,
                                                                std::string& outMessage,
                                                                std::vector<std::string>& outDetails,
                                                                std::string& outCode) {
    std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
    outDetails.clear();
    outCode = "OK";
    if (!authoringDocument_.HasMaterial()) {
        outMessage = "未打开材质文档";
        outCode = "MATERIAL_DOCUMENT_MISSING";
        return false;
    }
    if (authoringDocument_.HasMaterialGesture()) {
        outCode = "MATERIAL_EDIT_ACTIVE";
        outMessage = "Finish the active material edit first.";
        return false;
    }
    MaterialAssetData material = authoringDocument_.GetMaterial();
    MaterialGraphData* graph = material.kind == MaterialAssetKind::Material   ? &material.graph
                               : material.kind == MaterialAssetKind::Function ? &material.function.graph
                                                                              : nullptr;
    if (graph == nullptr) {
        outMessage = "当前资产没有可编辑的节点图";
        outCode = "NO_GRAPH";
        return false;
    }
    const auto findNode = [graph](const std::string& key) -> MaterialGraphNodeData* {
        for (MaterialGraphNodeData& node : graph->nodes) {
            if (node.id == key || node.displayName == key) {
                return &node;
            }
        }
        return nullptr;
    };

    if (request.operation == "add_node") {
        MaterialGraphNodeCreateRequest create{};
        create.nodeType = request.nodeType;
        create.displayName = request.name;
        create.position = {request.positionX, request.positionY};
        create.manualPosition = request.manualPosition;
        create.valueType = request.valueType;
        create.valueSet = request.valueSet;
        create.valueX = request.valueX;
        create.valueY = request.valueY;
        create.valueZ = request.valueZ;
        create.valueW = request.valueW;
        create.referenceId = request.referenceId;
        create.allowFunctionCall = false;
        return AddMaterialGraphNode(scene, create, outMessage, outDetails, outCode);
    }

    if (request.operation == "remove_node") {
        MaterialGraphNodeData* node = findNode(request.name);
        if (node == nullptr) {
            outMessage = "找不到节点：" + request.name;
            outCode = "NODE_NOT_FOUND";
            return false;
        }
        if (node->kind == MaterialNodeKind::SurfaceOutput) {
            outMessage = "Surface Output 是材质根节点，不能删除";
            outCode = "SURFACE_OUTPUT_PROTECTED";
            return false;
        }
        const std::string removedId = node->id;
        if (!authoringDocument_.RemoveMaterialNodes(std::span<const std::string>(&removedId, 1)))
            return false;
        (void)FlushMaterialPreview(scene, statusMessage_);
        outMessage = "已删除节点：" + removedId;
        return true;
    }

    if (request.operation == "set_node") {
        MaterialGraphNodeData* node = findNode(request.name);
        if (node == nullptr) {
            outMessage = "找不到节点：" + request.name;
            outCode = "NODE_NOT_FOUND";
            return false;
        }
        if (request.property == "valueType") {
            const std::optional<MaterialValueType> parsedType =
                MATERIAL_GRAPH_UI::ParseMaterialValueType(request.valueType);
            if (!parsedType.has_value()) {
                outMessage = "未知 valueType：" + request.valueType + "（支持 Float1/Float2/Float3/Float4/Color）";
                outCode = "INVALID_VALUE_TYPE";
                return false;
            }
            if (node->kind != MaterialNodeKind::Constant && node->kind != MaterialNodeKind::Parameter) {
                outMessage = "valueType 仅支持 Constant/Parameter 节点";
                outCode = "VALUE_TYPE_UNSUPPORTED";
                return false;
            }
            node->valueType = *parsedType;
            node->value.type = *parsedType;
            if (node->kind == MaterialNodeKind::Parameter) {
                MaterialGraphParameterData* parameter = FindMaterialGraphParameter(*graph, node->referenceId);
                if (parameter != nullptr) {
                    parameter->defaultValue.type = *parsedType;
                }
            }
        } else if (request.property == "value") {
            if (!request.valueSet) {
                outMessage = "缺少 valueX/valueY/valueZ/valueW 分量";
                outCode = "MISSING_VALUE";
                return false;
            }
            ApplyRequestedValueComponents(node->value, request);
            if (node->kind == MaterialNodeKind::Parameter) {
                MaterialGraphParameterData* parameter = FindMaterialGraphParameter(*graph, node->referenceId);
                if (parameter != nullptr) {
                    ApplyRequestedValueComponents(parameter->defaultValue, request);
                }
            }
        } else if (request.property == "displayName") {
            if (request.stringValue.empty()) {
                outMessage = "缺少 displayName 值（value 字段）";
                outCode = "MISSING_FIELD";
                return false;
            }
            node->displayName = request.stringValue;
        } else if (request.property == "resource") {
            if (node->kind != MaterialNodeKind::TextureSample) {
                outMessage = "resource 属性仅支持 TextureSample 节点";
                outCode = "RESOURCE_UNSUPPORTED";
                return false;
            }
            const auto resource = std::find_if(
                graph->resources.begin(), graph->resources.end(),
                [&request](const MaterialGraphResourceData& candidate) { return candidate.id == request.resource; });
            if (resource == graph->resources.end()) {
                outMessage = "找不到纹理资源：" + request.resource;
                outCode = "RESOURCE_NOT_FOUND";
                return false;
            }
            node->referenceId = resource->id;
        } else {
            outMessage =
                "未知 set_node 属性：" + request.property + "（支持 valueType / value / displayName / resource）";
            outCode = "UNKNOWN_PROPERTY";
            return false;
        }
        if (authoringDocument_.ApplyMaterial(material, "AI set material graph node"))
            (void)FlushMaterialPreview(scene, statusMessage_);
        outMessage = "已修改节点 " + node->id + " 的属性 " + request.property;
        return true;
    }

    if (request.operation == "connect") {
        MaterialGraphNodeData* sourceNode = findNode(request.sourceNode);
        MaterialGraphNodeData* targetNode = findNode(request.targetNode);
        if (sourceNode == nullptr) {
            outMessage = "找不到源节点：" + request.sourceNode;
            outCode = "NODE_NOT_FOUND";
            return false;
        }
        if (targetNode == nullptr) {
            outMessage = "找不到目标节点：" + request.targetNode;
            outCode = "NODE_NOT_FOUND";
            return false;
        }
        const std::optional<std::string> resolvedSourcePin = ResolvePin(*sourceNode, request.sourcePin, true);
        if (!resolvedSourcePin.has_value()) {
            outMessage = "源节点 " + sourceNode->id +
                         " 没有可用输出，"
                         "或找不到输出 pin：" +
                         request.sourcePin;
            outDetails.push_back(
                "outputs: " + MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeOutputPins(*sourceNode), "（无输出）"));
            outCode = "PIN_NOT_FOUND";
            return false;
        }
        const std::optional<std::string> resolvedTargetPin = ResolvePin(*targetNode, request.targetPin, false);
        if (!resolvedTargetPin.has_value()) {
            outMessage = "目标节点 " + targetNode->id +
                         " 没有可用输入，"
                         "或找不到输入 pin：" +
                         request.targetPin;
            outDetails.push_back("inputs: " +
                                 MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeInputPins(*targetNode), "（无输入）"));
            outCode = "PIN_NOT_FOUND";
            return false;
        }
        std::string reason;
        if (!authoringDocument_.ConnectMaterialNodes({"Edge_" + std::to_string(authoringIdCounter_++), sourceNode->id,
                                                      *resolvedSourcePin, targetNode->id, *resolvedTargetPin},
                                                     reason)) {
            outMessage = "Cannot connect: " + reason;
            outCode = "CONNECT_REJECTED";
            return false;
        }
        (void)FlushMaterialPreview(scene, statusMessage_);
        outMessage =
            "已连接 " + sourceNode->id + ":" + *resolvedSourcePin + " -> " + targetNode->id + ":" + *resolvedTargetPin;
        return true;
    }

    if (request.operation == "disconnect") {
        const auto findEdge = [&graph, &request, &findNode]() -> MaterialGraphEdgeData* {
            if (!request.edgeId.empty()) {
                for (MaterialGraphEdgeData& edge : graph->edges) {
                    if (edge.id == request.edgeId) {
                        return &edge;
                    }
                }
                return nullptr;
            }
            const MaterialGraphNodeData* sourceNode = findNode(request.sourceNode);
            const MaterialGraphNodeData* targetNode = findNode(request.targetNode);
            const std::optional<std::string> sourcePin =
                sourceNode != nullptr ? ResolvePin(*sourceNode, request.sourcePin, true) : std::nullopt;
            const std::optional<std::string> targetPin =
                targetNode != nullptr ? ResolvePin(*targetNode, request.targetPin, false) : std::nullopt;
            for (MaterialGraphEdgeData& edge : graph->edges) {
                const bool sourceMatches = sourceNode != nullptr && sourcePin.has_value() &&
                                           edge.sourceNodeId == sourceNode->id && edge.sourcePin == *sourcePin;
                const bool targetMatches = targetNode != nullptr && targetPin.has_value() &&
                                           edge.targetNodeId == targetNode->id && edge.targetPin == *targetPin;
                if (sourceMatches && targetMatches) {
                    return &edge;
                }
            }
            return nullptr;
        };
        MaterialGraphEdgeData* edge = findEdge();
        if (edge == nullptr) {
            outMessage = "找不到要断开的连线";
            outCode = "EDGE_NOT_FOUND";
            return false;
        }
        const std::string removedId = edge->id;
        if (!authoringDocument_.DisconnectMaterialEdge(removedId))
            return false;
        (void)FlushMaterialPreview(scene, statusMessage_);
        outMessage = "已断开连线：" + removedId;
        return true;
    }

    outMessage =
        "未知的 graph_edit 操作：" + request.operation + "（支持 add_node / remove_node / connect / disconnect）";
    outCode = "UNKNOWN_OPERATION";
    return false;
}

bool ModelMaterialWorkspaceController::QueryAiMaterialGraph(DocumentSceneBase& scene, const std::string& nameFilter,
                                                            std::string& outMessage,
                                                            std::vector<std::string>& outDetails,
                                                            std::string& outCode) {
    std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
    outDetails.clear();
    outCode = "OK";
    if (!authoringDocument_.HasMaterial()) {
        outMessage = "未打开材质文档";
        outCode = "MATERIAL_DOCUMENT_MISSING";
        return false;
    }
    const MaterialAssetData& material = authoringDocument_.GetMaterial();
    const auto* parent = material.kind == MaterialAssetKind::Instance
                             ? LoadInstanceParent(scene.GetAssetDatabase(), outMessage)
                             : nullptr;
    const MaterialGraphData* graph = parent                                         ? &parent->graph
                                     : material.kind == MaterialAssetKind::Material ? &material.graph
                                     : material.kind == MaterialAssetKind::Function ? &material.function.graph
                                                                                    : nullptr;
    if (graph == nullptr) {
        outMessage = "当前资产没有可编辑的节点图";
        outCode = "NO_GRAPH";
        return false;
    }
    const auto matches = [&nameFilter](const std::string& text) {
        return nameFilter.empty() || text.find(nameFilter) != std::string::npos;
    };
    for (const MaterialGraphNodeData& node : graph->nodes) {
        if (!matches(node.id) && !matches(node.displayName)) {
            continue;
        }
        std::string line = node.id + " | " + node.displayName + " | kind=" + node.kind.value +
                           " | valueType=" + std::string(ToString(node.valueType));
        if (node.kind == MaterialNodeKind::SurfaceOutput) {
            line += " | OUTPUT";
        }
        outDetails.push_back(line);
        outDetails.push_back("  inputs: " + MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeInputPins(node), "（无）"));
        outDetails.push_back("  outputs: " +
                             MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeOutputPins(node), "（无）"));
        for (const MaterialGraphEdgeData& edge : graph->edges) {
            if (edge.targetNodeId == node.id) {
                outDetails.push_back("  in " + edge.targetPin + " <- " + edge.sourceNodeId + "." + edge.sourcePin);
            }
        }
    }
    for (const auto& parameter : graph->parameters) {
        if (!matches(parameter.id) && !matches(parameter.displayName))
            continue;
        auto value = parameter.defaultValue;
        for (const auto& override : material.instance.parameterOverrides)
            if (override.parameterId == parameter.id) {
                value = override.value;
                break;
            }
        outDetails.push_back(parameter.id + " | " + MATERIAL_SERIALIZATION::WriteMaterialValueJson(value).dump());
    }
    if (outDetails.empty()) {
        outMessage = "没有匹配的材质图节点";
        outCode = "GRAPH_QUERY_EMPTY";
        return false;
    }
    outMessage = "材质图查询：";
    return true;
}

} // namespace HIKARI::EDITOR
