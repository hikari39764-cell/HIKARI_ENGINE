#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::AddMaterialGraphNode(DocumentSceneBase& scene,
                                                            const MaterialGraphNodeCreateRequest& request,
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
    const std::optional<MaterialNodeKind> resolvedKind = ResolveMaterialNodeKind(request.nodeType);
    if (!resolvedKind.has_value()) {
        outMessage = "未知节点类型：" + request.nodeType + "；可用类型（部分）：";
        size_t listed = 0u;
        for (const MaterialNodeDefinition& definition : GetMaterialNodeDefinitions()) {
            if (listed >= 24u) {
                break;
            }
            if (listed > 0u) {
                outMessage += " / ";
            }
            outMessage += definition.stableName;
            ++listed;
        }
        outMessage += "；成品节点例如 node.additive / node.skinLighting";
        outCode = "UNKNOWN_NODE_TYPE";
        return false;
    }
    const MaterialNodeKind kind = *resolvedKind;
    if (kind == MaterialNodeKind::FunctionCall) {
        if (!request.allowFunctionCall || !request.functionAssetGuid.IsValid()) {
            outMessage = "Material Function 节点必须绑定函数资产 GUID；"
                         "AI 暂不能创建，请在编辑器的 Material Functions 菜单添加。";
            outCode = "FUNCTION_CALL_UNSUPPORTED";
            return false;
        }
    }
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(kind);
    if (definition == nullptr) {
        outMessage = "节点类型未注册：" + request.nodeType;
        outCode = "NODE_TYPE_UNREGISTERED";
        return false;
    }
    if (ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
        std::any_of(graph->nodes.begin(), graph->nodes.end(),
                    [kind](const MaterialGraphNodeData& node) { return node.kind == kind; })) {
        outMessage = "该材质已经包含单例节点：" + std::string(definition->stableName);
        outCode = "SINGLETON_NODE_EXISTS";
        return false;
    }
    if (kind == MaterialNodeKind::SurfaceOutput && !graph->outputNodeId.empty()) {
        outMessage = "材质已经有一个 Surface Output 节点";
        outCode = "SURFACE_OUTPUT_EXISTS";
        return false;
    }
    MaterialGraphNodeData node{};
    node.id = "Node_" + std::to_string(authoringIdCounter_++);
    node.kind = kind;
    node.displayName = request.displayName.empty() ? std::string(definition->stableName) : request.displayName;
    node.position = request.manualPosition ? request.position : MATH::Vec2{40.0f, 40.0f};
    if (kind == MaterialNodeKind::FunctionCall) {
        node.assetReference = request.functionAssetGuid;
        node.functionInputs = request.functionInputs;
        node.functionOutputs = request.functionOutputs;
        node.valueType = node.functionOutputs.empty() ? MATERIAL_GRAPH_UI::DefaultNodeValueType(kind)
                                                      : node.functionOutputs.front().defaultValue.type;
        node.value.type = node.valueType;
    } else if (kind == MaterialNodeKind::Variable) {
        const BINDING::VariableDefinition* variable =
            scene.GetRuntimeBindingStore().GetCatalog().FindVariable(request.referenceId);
        if (variable == nullptr) {
            outMessage = "Runtime variable was not found: " + request.referenceId;
            outCode = "RUNTIME_VARIABLE_NOT_FOUND";
            return false;
        }
        const std::optional<MaterialValueType> materialType =
            BINDING::MATERIAL_ADAPTER::ToMaterialValueType(variable->defaultValue.type);
        if (!materialType.has_value() ||
            !BINDING::MATERIAL_ADAPTER::TryConvertValue(variable->defaultValue, *materialType, node.value)) {
            outMessage = "Runtime variable type cannot be consumed by Material: " + variable->id;
            outCode = "RUNTIME_VARIABLE_TYPE_UNSUPPORTED";
            return false;
        }
        node.referenceId = variable->id;
        node.valueType = *materialType;
        if (request.displayName.empty()) {
            node.displayName = variable->name;
        }
    } else {
        if (!request.valueType.empty()) {
            const std::optional<MaterialValueType> parsedType =
                MATERIAL_GRAPH_UI::ParseMaterialValueType(request.valueType);
            if (!parsedType.has_value()) {
                outMessage = "未知 valueType：" + request.valueType + "（支持 Float1/Float2/Float3/Float4/Color）";
                outCode = "INVALID_VALUE_TYPE";
                return false;
            }
            if (kind != MaterialNodeKind::Constant && kind != MaterialNodeKind::Parameter) {
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
        if ((kind == MaterialNodeKind::Constant || kind == MaterialNodeKind::Parameter) && request.valueSet) {
            node.value.numeric = {request.valueX, request.valueY, request.valueZ, request.valueW};
        }
        if (kind == MaterialNodeKind::RampDiffuse || kind == MaterialNodeKind::MultiRampDiffuse ||
            kind == MaterialNodeKind::FaceSdf) {
            (void)BindBuiltInRampPresets(material);
        }
        if (kind == MaterialNodeKind::Parameter) {
            std::string parameterId = request.parameterIdHint;
            if (parameterId.empty()) {
                const std::string requestedId =
                    request.displayName.empty()
                        ? std::string{}
                        : (request.displayName.rfind("parameter.", 0u) == 0u ? request.displayName
                                                                             : "parameter." + request.displayName);
                parameterId =
                    requestedId.empty()
                        ? (request.parameterIdPrefix.empty() ? "parameter.ai_" : request.parameterIdPrefix) + node.id
                        : requestedId;
            }
            const auto existing = std::find_if(graph->parameters.begin(), graph->parameters.end(),
                                               [&parameterId](const MaterialGraphParameterData& parameter) {
                                                   return !parameterId.empty() && parameter.id == parameterId;
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
                               ? (request.displayName.empty() ? "AI Parameter" : request.displayName)
                               : "Parameter");
                parameter.defaultValue.type = node.valueType;
                if (request.valueSet) {
                    parameter.defaultValue.numeric = {request.valueX, request.valueY, request.valueZ, request.valueW};
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
    const std::string nodeId = node.id;
    const std::string displayName = node.displayName;
    graph->nodes.push_back(std::move(node));
    if (!request.connectSourceNodeId.empty()) {
        std::string targetPin, reason;
        for (const auto pin : MATERIAL_GRAPH_UI::InputPins(graph->nodes.back())) {
            if (CanConnectMaterialGraphPins(*graph, request.connectSourceNodeId, request.connectSourcePin, nodeId, pin,
                                            reason)) {
                targetPin = pin;
                break;
            }
        }
        if (targetPin.empty()) {
            outCode = "INCOMPATIBLE_NODE";
            outMessage = TEXT::Localize("material.palette.incompatible");
            return false;
        }
        graph->edges.push_back({"Edge_" + std::to_string(authoringIdCounter_++), request.connectSourceNodeId,
                                request.connectSourcePin, nodeId, targetPin});
    }
    if (!request.manualPosition)
        MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(*graph, {nodeId});
    SetGraphNodeSelection(nodeId, false);
    if (authoringDocument_.ApplyMaterial(material, "Add material graph node"))
        (void)FlushMaterialPreview(scene, statusMessage_);
    outMessage = TEXT::Localize("material.node.added") + " " +
                 (request.displayName.empty() || request.displayName == definition->stableName
                      ? MATERIAL_GRAPH_UI::NodeDisplayName(kind)
                      : displayName);
    const MaterialGraphNodeData* added = FindMaterialGraphNode(*graph, nodeId);
    if (added != nullptr) {
        if (!added->referenceId.empty()) {
            outDetails.push_back("referenceId: " + added->referenceId);
        }
        outDetails.push_back("inputs: " + MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeInputPins(*added), "none"));
        outDetails.push_back("outputs: " + MATERIAL_GRAPH_UI::JoinPins(GetMaterialGraphNodeOutputPins(*added), "none"));
    }
    return true;
}

} // namespace HIKARI::EDITOR
