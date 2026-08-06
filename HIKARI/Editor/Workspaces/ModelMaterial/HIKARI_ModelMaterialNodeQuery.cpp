#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"

#include <algorithm>
#include <string>

namespace HIKARI::EDITOR {
namespace {

std::string LowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string SemanticClassName(MaterialNodeSemanticClass value) {
    switch (value) {
    case MaterialNodeSemanticClass::Input: return "Input";
    case MaterialNodeSemanticClass::Math: return "Math";
    case MaterialNodeSemanticClass::Surface: return "Surface";
    case MaterialNodeSemanticClass::Shading: return "Shading";
    case MaterialNodeSemanticClass::Function: return "Function";
    }
    return "Other";
}

std::string OutputRouteName(MaterialNodeOutputRoute value) {
    switch (value) {
    case MaterialNodeOutputRoute::Unrestricted: return "Unrestricted";
    case MaterialNodeOutputRoute::DiffuseBase: return "DiffuseBase";
    case MaterialNodeOutputRoute::DiffuseAfterSkin: return "DiffuseAfterSkin";
    case MaterialNodeOutputRoute::DiffuseFinal: return "DiffuseFinal";
    case MaterialNodeOutputRoute::Specular: return "Specular";
    case MaterialNodeOutputRoute::Eye: return "Eye";
    case MaterialNodeOutputRoute::Additive: return "Additive";
    case MaterialNodeOutputRoute::Outline: return "Outline";
    }
    return "Unrestricted";
}

std::string FormatPins(
    std::span<const MaterialNodePinDefinition> pins,
    bool inputs) {
    std::string result;
    for (const MaterialNodePinDefinition& pin : pins) {
        if (!result.empty()) {
            result += ", ";
        }
        result += std::string(pin.id) + "(" +
            std::string(ToString(pin.fixedType));
        if (inputs && pin.acceptsDynamicNumeric) {
            result += ", dynamic-numeric";
        }
        result += ")";
    }
    return result.empty() ? "none" : result;
}

} // namespace

std::optional<MaterialNodeKind> ResolveMaterialNodeKind(
    std::string_view input) noexcept {
    if (const MaterialNodeDefinition* definition =
            FindMaterialNodeDefinition(input)) {
        return definition->kind;
    }
    std::string normalized(input);
    if (normalized.rfind("node.", 0u) == 0u) {
        normalized = normalized.substr(5u);
    }
    const std::string lowered = LowerAscii(normalized);
    if (lowered == "additive") {
        return MaterialNodeKind::Add;
    }
    for (const MaterialNodeDefinition& definition :
         GetMaterialNodeDefinitions()) {
        if (LowerAscii(std::string(definition.stableName)) == lowered) {
            return definition.kind;
        }
    }
    return std::nullopt;
}

bool DescribeAiMaterialNode(
    std::string_view nodeType,
    std::string& outMessage,
    std::vector<std::string>& outDetails,
    std::string& outCode) {
    outDetails.clear();
    const std::optional<MaterialNodeKind> kind =
        ResolveMaterialNodeKind(nodeType);
    if (!kind.has_value()) {
        outCode = "NODE_TYPE_NOT_FOUND";
        outMessage = "未知节点类型：" + std::string(nodeType);
        outDetails.push_back(
            "可用 material.node_info 查询的节点名示例："
            "noise.value / math.fresnel / uv.transform / "
            "TextureSample / Add / Parameter / SurfaceOutput");
        return false;
    }
    const MaterialNodeDefinition* definition =
        FindMaterialNodeDefinition(*kind);
    if (definition == nullptr) {
        outCode = "NODE_DEFINITION_MISSING";
        outMessage = "节点类型已解析但缺少定义：" + std::string(nodeType);
        return false;
    }
    outCode = "OK";
    outMessage = "节点信息：" + std::string(definition->stableName);
    outDetails.push_back(
        "stableName: " + std::string(definition->stableName));
    outDetails.push_back("class: " + SemanticClassName(definition->semanticClass));
    outDetails.push_back("summary: " + std::string(definition->summary));
    outDetails.push_back(
        "inputs: " + FormatPins(definition->inputs, true));
    outDetails.push_back(
        "outputs: " + FormatPins(definition->outputs, false));
    outDetails.push_back(
        std::string("staticallyEvaluable: ") +
        (definition->staticallyEvaluable ? "true" : "false"));
    outDetails.push_back(
        "outputRoute: " + OutputRouteName(definition->outputRoute));
    std::string editable = "displayName";
    if (definition->kind == MaterialNodeKind::Constant ||
        definition->kind == MaterialNodeKind::Parameter) {
        editable += ", value, valueType";
    }
    if (definition->kind == MaterialNodeKind::TextureSample) {
        editable += ", resource";
    }
    outDetails.push_back("editableProperties: " + editable);
    return true;
}

bool ListAiMaterialNodes(
    std::string_view keyword,
    std::string_view nodeClass,
    std::string& outMessage,
    std::vector<std::string>& outDetails,
    std::string& outCode) {
    outDetails.clear();
    const std::string foldedKeyword = LowerAscii(std::string(keyword));
    std::string normalizedClass = LowerAscii(std::string(nodeClass));
    for (const MaterialNodeDefinition& definition :
         GetMaterialNodeDefinitions()) {
        if (!definition.paletteVisible) {
            continue;
        }
        const std::string className =
            SemanticClassName(definition.semanticClass);
        if (!normalizedClass.empty() &&
            LowerAscii(className) != normalizedClass &&
            LowerAscii(std::string(definition.stableName)) !=
                normalizedClass) {
            continue;
        }
        if (!foldedKeyword.empty()) {
            const std::string haystack = LowerAscii(
                std::string(definition.stableName) + " " +
                std::string(definition.summary));
            if (haystack.find(foldedKeyword) == std::string::npos) {
                continue;
            }
        }
        outDetails.push_back(
            std::string(definition.stableName) + " | " +
            className + " | " + std::string(definition.summary));
    }
    if (outDetails.empty()) {
        outCode = "NODE_LIST_EMPTY";
        outMessage = "没有找到匹配的节点";
        outDetails.push_back(
            "可用 class 过滤：Input / Math / Shading / Surface；"
            "或用 keyword 搜索节点名/语义，例如 noise / uv / color");
        return false;
    }
    outCode = "OK";
    outMessage = "可用节点：" + std::to_string(outDetails.size()) +
        " 个；可用 material.node_info 查看单个节点详情";
    return true;
}

} // namespace HIKARI::EDITOR
