#include "HIKARI_MaterialGraphJson.h"

#include <algorithm>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

    namespace {

        namespace JsonMath = SERIALIZATION::JSON::MATH;

        template<class TValue, class TReader>
        bool ReadArray(
            const nlohmann::json& parent,
            const char* key,
            std::vector<TValue>& outValues,
            TReader&& reader,
            std::string& outError) {

            outValues.clear();
            const auto array = parent.find(key);
            if (array == parent.end()) {
                return true;
            }
            if (!array->is_array()) {
                outError = std::string("material field must be an array: ") + key;
                return false;
            }
            outValues.reserve(array->size());
            for (const nlohmann::json& item : *array) {
                TValue value{};
                if (!reader(item, value, outError)) {
                    return false;
                }
                outValues.push_back(std::move(value));
            }
            return true;
        }

        bool ReadResource(
            const nlohmann::json& node,
            MaterialGraphResourceData& outResource,
            std::string& outError) {

            if (!node.is_object()) {
                outError = "material graph resource must be an object";
                return false;
            }
            MaterialGraphResourceData resource{};
            resource.id = node.value("id", std::string{});
            resource.displayName = node.value("displayName", resource.id);
            if (!TryParseMaterialResourceType(
                    node.value("type", std::string{ "Texture2D" }),
                    resource.type)) {
                outError = "unknown material resource type for " + resource.id;
                return false;
            }
            if (!TryParseMaterialResourceSemantic(
                    node.value("semantic", std::string{ "Custom" }),
                    resource.semantic)) {
                outError = "unknown material resource semantic for " + resource.id;
                return false;
            }
            resource.assetGuid.value = node.value("assetGuid", std::string{});
            resource.enabled = node.value("enabled", resource.assetGuid.IsValid());
            resource.texCoord = node.value("texCoord", resource.texCoord);
            resource.uvScale = JSONREAD::Vec2Or(
                node.value("uvScale", nlohmann::json::array()),
                resource.uvScale);
            resource.uvOffset = JSONREAD::Vec2Or(
                node.value("uvOffset", nlohmann::json::array()),
                resource.uvOffset);
            resource.uvRotation = node.value("uvRotation", resource.uvRotation);
            outResource = std::move(resource);
            return true;
        }

        nlohmann::json WriteResource(
            const MaterialGraphResourceData& resource) {

            return nlohmann::json{
                { "id", resource.id },
                { "displayName", resource.displayName },
                { "type", std::string(ToString(resource.type)) },
                { "semantic", std::string(ToString(resource.semantic)) },
                { "assetGuid", resource.assetGuid.value },
                { "enabled", resource.enabled },
                { "texCoord", resource.texCoord },
                { "uvScale", JsonMath::ToJsonArray(resource.uvScale) },
                { "uvOffset", JsonMath::ToJsonArray(resource.uvOffset) },
                { "uvRotation", resource.uvRotation },
            };
        }

        bool ReadProperty(
            const nlohmann::json& node,
            MaterialNodePropertyData& outProperty,
            std::string& outError) {

            if (!node.is_object()) {
                outError = "material node property must be an object";
                return false;
            }
            MaterialNodePropertyData property{};
            property.id = node.value("id", std::string{});
            if (!ReadMaterialValueJson(node.value("value", nlohmann::json{}),
                    property.value, outError)) {
                return false;
            }
            outProperty = std::move(property);
            return true;
        }

        nlohmann::json WriteProperty(
            const MaterialNodePropertyData& property) {

            return nlohmann::json{
                { "id", property.id },
                { "value", WriteMaterialValueJson(property.value) },
            };
        }

        nlohmann::json WriteFunctionPins(
            std::vector<MaterialGraphParameterData> pins) {
            std::sort(pins.begin(), pins.end(), [](const auto& left, const auto& right) {
                return left.id < right.id;
            });
            nlohmann::json values = nlohmann::json::array();
            for (const MaterialGraphParameterData& pin : pins) {
                values.push_back(WriteMaterialParameterJson(pin));
            }
            return values;
        }

        bool ReadNode(
            const nlohmann::json& node,
            MaterialGraphNodeData& outNode,
            std::string& outError) {

            if (!node.is_object()) {
                outError = "material graph node must be an object";
                return false;
            }
            MaterialGraphNodeData graphNode{};
            graphNode.id = node.value("id", std::string{});
            if (!TryParseMaterialNodeTypeId(
                    node.value("kind", std::string{}),
                    graphNode.kind)) {
                outError = "invalid material node type ID for " + graphNode.id;
                return false;
            }
            graphNode.displayName = node.value("displayName", graphNode.id);
            graphNode.position = JSONREAD::Vec2Or(
                node.value("position", nlohmann::json::array()),
                graphNode.position);
            if (!TryParseMaterialValueType(
                    node.value("valueType", std::string{ "Float1" }),
                    graphNode.valueType)) {
                outError = "unknown material node value type for " + graphNode.id;
                return false;
            }
            if (const auto value = node.find("value"); value != node.end() &&
                !ReadMaterialValueJson(*value, graphNode.value, outError)) {
                return false;
            }
            graphNode.referenceId = node.value("referenceId", std::string{});
            graphNode.assetReference.value = node.value("assetReference", std::string{});
            if (!ReadArray(node, "properties", graphNode.properties,
                    ReadProperty, outError)) {
                return false;
            }
            if (!ReadArray(node, "functionInputs", graphNode.functionInputs,
                    ReadMaterialParameterJson, outError) ||
                !ReadArray(node, "functionOutputs", graphNode.functionOutputs,
                    ReadMaterialParameterJson, outError)) {
                return false;
            }
            outNode = std::move(graphNode);
            return true;
        }

        nlohmann::json WriteNode(const MaterialGraphNodeData& node) {
            std::vector<MaterialNodePropertyData> properties = node.properties;
            std::sort(properties.begin(), properties.end(), [](const auto& left, const auto& right) {
                return left.id < right.id;
            });
            nlohmann::json propertyArray = nlohmann::json::array();
            for (const MaterialNodePropertyData& property : properties) {
                propertyArray.push_back(WriteProperty(property));
            }
            return nlohmann::json{
                { "id", node.id },
                { "kind", std::string(ToString(node.kind)) },
                { "displayName", node.displayName },
                { "position", JsonMath::ToJsonArray(node.position) },
                { "valueType", std::string(ToString(node.valueType)) },
                { "value", WriteMaterialValueJson(node.value) },
                { "referenceId", node.referenceId },
                { "assetReference", node.assetReference.value },
                { "properties", std::move(propertyArray) },
                { "functionInputs", WriteFunctionPins(node.functionInputs) },
                { "functionOutputs", WriteFunctionPins(node.functionOutputs) },
            };
        }

        bool ReadEdge(
            const nlohmann::json& node,
            MaterialGraphEdgeData& outEdge,
            std::string& outError) {

            if (!node.is_object()) {
                outError = "material graph edge must be an object";
                return false;
            }
            outEdge.id = node.value("id", std::string{});
            outEdge.sourceNodeId = node.value("sourceNodeId", std::string{});
            outEdge.sourcePin = node.value("sourcePin", std::string{});
            outEdge.targetNodeId = node.value("targetNodeId", std::string{});
            outEdge.targetPin = node.value("targetPin", std::string{});
            return true;
        }

        nlohmann::json WriteEdge(const MaterialGraphEdgeData& edge) {
            return nlohmann::json{
                { "id", edge.id },
                { "sourceNodeId", edge.sourceNodeId },
                { "sourcePin", edge.sourcePin },
                { "targetNodeId", edge.targetNodeId },
                { "targetPin", edge.targetPin },
            };
        }

    } // namespace

    bool ReadMaterialValueJson(
        const nlohmann::json& node,
        MaterialValueData& outValue,
        std::string& outError) {

        if (!node.is_object()) {
            outError = "material value must be an object";
            return false;
        }
        MaterialValueData value{};
        if (!TryParseMaterialValueType(
                node.value("type", std::string{ "Float1" }),
                value.type)) {
            outError = "unknown material value type";
            return false;
        }
        if (value.type == MaterialValueType::Bool) {
            value.boolean = node.value("value", false);
        } else if (IsMaterialNumericType(value.type)) {
            value.numeric = JSONREAD::Vec4Or(
                node.value("value", nlohmann::json::array()),
                value.numeric);
        } else if (node.contains("value")) {
            outError = "material connection-only value type cannot contain a literal";
            return false;
        }
        outValue = value;
        return true;
    }

    nlohmann::json WriteMaterialValueJson(
        const MaterialValueData& value) {

        nlohmann::json node{
            { "type", std::string(ToString(value.type)) },
        };
        if (value.type == MaterialValueType::Bool) {
            node["value"] = value.boolean;
        } else if (IsMaterialNumericType(value.type)) {
            node["value"] = JsonMath::ToJsonArray(value.numeric);
        }
        return node;
    }

    bool ReadMaterialParameterJson(
        const nlohmann::json& node,
        MaterialGraphParameterData& outParameter,
        std::string& outError) {

        if (!node.is_object()) {
            outError = "material parameter must be an object";
            return false;
        }
        MaterialGraphParameterData parameter{};
        parameter.id = node.value("id", std::string{});
        parameter.displayName = node.value("displayName", parameter.id);
        parameter.exposed = node.value("exposed", true);
        if (!ReadMaterialValueJson(
                node.value("defaultValue", nlohmann::json{}),
                parameter.defaultValue,
                outError)) {
            return false;
        }
        outParameter = std::move(parameter);
        return true;
    }

    nlohmann::json WriteMaterialParameterJson(
        const MaterialGraphParameterData& parameter) {

        return nlohmann::json{
            { "id", parameter.id },
            { "displayName", parameter.displayName },
            { "defaultValue", WriteMaterialValueJson(parameter.defaultValue) },
            { "exposed", parameter.exposed },
        };
    }

    bool ReadMaterialGraphJson(
        const nlohmann::json& node,
        MaterialGraphData& outGraph,
        std::string& outError) {

        if (!node.is_object()) {
            outError = "material graph must be an object";
            return false;
        }
        MaterialGraphData graph{};
        graph.outputNodeId = node.value("outputNodeId", std::string{});
        if (!ReadArray(node, "parameters", graph.parameters,
                ReadMaterialParameterJson, outError) ||
            !ReadArray(node, "resources", graph.resources,
                ReadResource, outError) ||
            !ReadArray(node, "nodes", graph.nodes,
                ReadNode, outError) ||
            !ReadArray(node, "edges", graph.edges,
                ReadEdge, outError)) {
            return false;
        }
        outGraph = std::move(graph);
        return true;
    }

    nlohmann::json WriteMaterialGraphJson(
        const MaterialGraphData& graph) {

        auto parameters = graph.parameters;
        auto resources = graph.resources;
        auto nodes = graph.nodes;
        auto edges = graph.edges;
        const auto byId = [](const auto& left, const auto& right) {
            return left.id < right.id;
        };
        std::sort(parameters.begin(), parameters.end(), byId);
        std::sort(resources.begin(), resources.end(), byId);
        std::sort(nodes.begin(), nodes.end(), byId);
        std::sort(edges.begin(), edges.end(), byId);

        nlohmann::json parameterArray = nlohmann::json::array();
        for (const auto& parameter : parameters) {
            parameterArray.push_back(WriteMaterialParameterJson(parameter));
        }
        nlohmann::json resourceArray = nlohmann::json::array();
        for (const auto& resource : resources) {
            resourceArray.push_back(WriteResource(resource));
        }
        nlohmann::json nodeArray = nlohmann::json::array();
        for (const auto& graphNode : nodes) {
            nodeArray.push_back(WriteNode(graphNode));
        }
        nlohmann::json edgeArray = nlohmann::json::array();
        for (const auto& edge : edges) {
            edgeArray.push_back(WriteEdge(edge));
        }
        return nlohmann::json{
            { "outputNodeId", graph.outputNodeId },
            { "parameters", std::move(parameterArray) },
            { "resources", std::move(resourceArray) },
            { "nodes", std::move(nodeArray) },
            { "edges", std::move(edgeArray) },
        };
    }

} // namespace HIKARI::MATERIAL_SERIALIZATION
