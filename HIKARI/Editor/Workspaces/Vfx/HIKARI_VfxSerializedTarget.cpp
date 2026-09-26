#include "Editor/Workspaces/Vfx/HIKARI_VfxSerializedTarget.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

namespace HIKARI::EDITOR {
namespace {

using Json = nlohmann::json;

template <class Node>
Node* FindById(Node& node, std::string_view targetId) noexcept {
    if (node.is_object()) {
        const auto id = node.find("id");
        if (id != node.end() && id->is_string() && id->template get_ref<const std::string&>() == targetId)
            return &node;
    }
    if (node.is_structured())
        for (auto& child : node)
            if (auto* found = FindById(child, targetId))
                return found;
    return nullptr;
}

std::vector<std::string> SplitPath(std::string_view path) {
    std::vector<std::string> result{};
    size_t start = 0u;
    while (start <= path.size()) {
        const size_t separator = path.find('.', start);
        const size_t end = separator == std::string_view::npos ? path.size() : separator;
        if (end == start)
            return {};
        result.emplace_back(path.substr(start, end - start));
        if (separator == std::string_view::npos)
            break;
        start = separator + 1u;
    }
    return result;
}

bool IsNumericArray(const Json& value) noexcept {
    if (!value.is_array() || value.empty())
        return false;
    for (const Json& component : value) {
        if (!component.is_number())
            return false;
    }
    return true;
}

bool HasCompatibleShape(const Json& current, const Json& replacement) noexcept {
    if (current.is_boolean())
        return replacement.is_boolean();
    if (current.is_number())
        return replacement.is_number();
    if (current.is_string())
        return replacement.is_string();
    if (current.is_object()) {
        if (!replacement.is_object() || current.contains("id") || current.size() != replacement.size()) {
            return false;
        }
        for (const auto& [name, child] : current.items()) {
            const auto candidate = replacement.find(name);
            if (candidate == replacement.end() || !HasCompatibleShape(child, *candidate)) {
                return false;
            }
        }
        return true;
    }
    if (IsNumericArray(current)) {
        return IsNumericArray(replacement) && replacement.size() == current.size();
    }
    return false;
}

Json DefaultInputValue(VFX::VfxValueType type) {
    switch (type) {
    case VFX::VfxValueType::Bool: return false;
    case VFX::VfxValueType::Int: return 0;
    case VFX::VfxValueType::Float: return 0.0f;
    case VFX::VfxValueType::Float2: return Json::array({0.0f, 0.0f});
    case VFX::VfxValueType::Float3: return Json::array({0.0f, 0.0f, 0.0f});
    case VFX::VfxValueType::Float4: return Json::array({0.0f, 0.0f, 0.0f, 0.0f});
    case VFX::VfxValueType::Color: return Json::array({1.0f, 1.0f, 1.0f, 1.0f});
    }
    return 0.0f;
}

float ComponentOr(const Json& value, size_t index, float fallback) noexcept {
    if (value.is_number())
        return value.get<float>();
    if (value.is_array() && index < value.size() && value[index].is_number()) {
        return value[index].get<float>();
    }
    return fallback;
}

void EnsureCurveKeys(Json& animated, const VfxStableIdAllocator& allocateId) {
    const auto curve = animated.find("curve");
    if (curve == animated.end() || !curve->is_object())
        return;
    const Json fixed = animated.value("fixed", Json::object());
    const Json minimum = fixed.value("minimum", Json{});
    size_t component = 0u;
    for (auto& [channel, channelCurve] : curve->items()) {
        (void)channel;
        if (!channelCurve.is_object()) {
            ++component;
            continue;
        }
        Json& keys = channelCurve["keys"];
        if (!keys.is_array())
            keys = Json::array();
        if (keys.empty()) {
            const float defaultValue = ComponentOr(minimum, component, 0.0f);
            keys.push_back({
                {"id", allocateId("curve.key")},
                {"time", 0.0f},
                {"value", defaultValue},
                {"inTangent", 0.0f},
                {"outTangent", 0.0f},
            });
            keys.push_back({
                {"id", allocateId("curve.key")},
                {"time", 1.0f},
                {"value", defaultValue},
                {"inTangent", 0.0f},
                {"outTangent", 0.0f},
            });
        }
        ++component;
    }
}

void EnsureGradientStops(Json& animated, const VfxStableIdAllocator& allocateId) {
    Json& gradient = animated["gradient"];
    if (!gradient.is_object())
        gradient = Json::object();
    Json& stops = gradient["stops"];
    if (!stops.is_array())
        stops = Json::array();
    if (!stops.empty())
        return;
    const Json fixed = animated.value("fixed", Json::object());
    const Json color = fixed.value("minimum", Json::array({1.0f, 1.0f, 1.0f, 1.0f}));
    stops.push_back({
        {"id", allocateId("gradient.stop")},
        {"time", 0.0f},
        {"color", color},
    });
    Json endColor = color;
    if (endColor.is_array() && endColor.size() == 4u) {
        endColor[3] = 0.0f;
    }
    stops.push_back({
        {"id", allocateId("gradient.stop")},
        {"time", 1.0f},
        {"color", std::move(endColor)},
    });
}

Json ChoicesFor(std::string_view path, const Json& parent, const Json& value) {
    const size_t separator = path.find_last_of('.');
    const std::string_view leaf = separator == std::string_view::npos ? path : path.substr(separator + 1u);
    if (leaf == "mode") {
        if (parent.contains("samples") && parent.contains("breakDistance"))
            return Json(VFX::kVfxHistoryModeChoices);
        if (parent.contains("socketName") && parent.contains("offset")) {
            return Json(VFX::kVfxAttachmentModeChoices);
        }
        if (parent.contains("pva")) {
            return Json(VFX::kVfxAnimatedValueModeChoices);
        }
        if (parent.contains("gradient")) {
            return Json(VFX::kVfxAnimatedColorModeChoices);
        }
        if (parent.contains("minimum") && parent.contains("maximum")) {
            return Json(VFX::kVfxDistributionModeChoices);
        }
    }
    if (leaf == "follow" && parent.contains("socketName") && parent.contains("offset")) {
        return Json(VFX::kVfxFollowModeChoices);
    }
    if (leaf == "interpolation") {
        return Json(VFX::kVfxCurveInterpolationChoices);
    }
    if (leaf == "function") {
        return Json(VFX::kVfxEasingFunctionChoices);
    }
    if (leaf == "blendMode")
        return Json(VFX::kVfxBlendModeChoices);
    if (leaf == "materialMode")
        return Json(VFX::kVfxMaterialModeChoices);
    if (leaf == "depthMode") {
        return Json(VFX::kVfxParticleDepthModeChoices);
    }
    if (leaf == "kind" && parent.contains("startDelay"))
        return Json(VFX::kVfxLayerKindChoices);
    if (leaf == "kind" && parent.contains("parameters")) {
        return Json(GetMaterialEffectKindNames());
    }
    if (leaf == "capacityMode") {
        return Json(VFX::kVfxParticleCapacityModeChoices);
    }
    if (leaf == "billboardMode") {
        return Json(VFX::kVfxBillboardModeChoices);
    }
    if (leaf == "billboardAxis") {
        return Json(VFX::kVfxBillboardAxisChoices);
    }
    if (leaf == "mode" && parent.contains("targetLayerId")) {
        return Json(VFX::kVfxConnectTargetModeChoices);
    }
    if (leaf == "uvMode") {
        return Json(VFX::kVfxShapeUvModeChoices);
    }
    if (leaf == "type" && value.is_string()) {
        const std::string& current = value.get_ref<const std::string&>();
        if (parent.contains("weaponPathDistortion") && parent.contains("screenImpulse") &&
            parent.contains("skinnedAfterimage") && parent.contains("light")) {
            return Json(VFX::kVfxSpecialOutputTypeChoices);
        }
        if (TEXT::TryParseEnum(VFX::kVfxShapeTypeNames, current)) {
            return Json(VFX::kVfxShapeTypeChoices);
        }
        if (parent.contains("billboardMode")) {
            return Json(VFX::kVfxRenderShapeTypeChoices);
        }
        return Json(VFX::kVfxValueTypeChoices);
    }
    return nullptr;
}

std::string PropertyType(const Json& value) {
    if (value.is_boolean())
        return "bool";
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return "int";
    }
    if (value.is_number_float())
        return "float";
    if (value.is_string())
        return "string";
    if (IsNumericArray(value)) {
        return value.size() == 2u   ? "float2"
               : value.size() == 3u ? "float3"
               : value.size() == 4u ? "float4"
                                    : "numberArray";
    }
    return "json";
}

void FlattenProperties(const Json& node, std::string path, Json& outProperties) {
    if (!node.is_object())
        return;
    for (const auto& [name, value] : node.items()) {
        if (name == "id")
            continue;
        const std::string childPath = path.empty() ? name : path + "." + name;
        if (value.is_object()) {
            FlattenProperties(value, childPath, outProperties);
            continue;
        }
        if (value.is_array() && !IsNumericArray(value)) {
            continue;
        }
        if (!value.is_boolean() && !value.is_number() && !value.is_string() && !IsNumericArray(value)) {
            continue;
        }
        Json property{
            {"id", childPath},
            {"type", PropertyType(value)},
            {"value", value},
        };
        const Json choices = ChoicesFor(childPath, node, value);
        if (!choices.is_null()) {
            property["type"] = "choice";
            property["choices"] = choices;
        }
        outProperties.push_back(std::move(property));
    }
}

} // namespace

bool BuildVfxSerializedDocument(const VFX::VfxAssetData& asset, nlohmann::json& outDocument, std::string& outMessage) {
    std::string text{};
    if (!VFX::SERIALIZATION::SerializeVfxSourceJson(asset, text, outMessage)) {
        return false;
    }
    try {
        outDocument = Json::parse(text);
    } catch (const Json::exception& exception) {
        outMessage = "Could not inspect VFX source JSON: " + std::string(exception.what());
        return false;
    }
    outMessage.clear();
    return true;
}

bool ParseVfxSerializedDocument(const nlohmann::json& document, VFX::VfxAssetData& outAsset, std::string& outMessage) {
    return VFX::SERIALIZATION::ParseVfxSourceJson(document.dump(), outAsset, outMessage);
}

nlohmann::json* FindVfxSerializedTarget(nlohmann::json& document, std::string_view targetId) noexcept {
    return targetId == "asset" ? &document : FindById(document, targetId);
}

const nlohmann::json* FindVfxSerializedTarget(const nlohmann::json& document, std::string_view targetId) noexcept {
    return targetId == "asset" ? &document : FindById(document, targetId);
}

const nlohmann::json* FindVfxSerializedProperty(const nlohmann::json& target, std::string_view propertyPath) {
    const std::vector<std::string> path = SplitPath(propertyPath);
    if (path.empty())
        return nullptr;
    const Json* value = &target;
    for (const std::string& part : path) {
        if (!value->is_object())
            return nullptr;
        const auto child = value->find(part);
        if (child == value->end())
            return nullptr;
        value = &*child;
    }
    return value;
}

bool SetVfxSerializedProperty(nlohmann::json& document, std::string_view targetId, std::string_view propertyPath,
                              const nlohmann::json& value, const VfxStableIdAllocator& allocateId,
                              std::string& outMessage) {
    Json* target = FindVfxSerializedTarget(document, targetId);
    if (target == nullptr || !target->is_object()) {
        outMessage = "VFX target was not found: " + std::string(targetId);
        return false;
    }
    const std::vector<std::string> path = SplitPath(propertyPath);
    if (path.empty()) {
        outMessage = "VFX property path is empty or malformed";
        return false;
    }
    if (targetId == "asset" && (path.front() == "version" || path.front() == "inputs" || path.front() == "emitters")) {
        outMessage = "VFX structural property is read-only";
        return false;
    }
    Json* parent = target;
    for (size_t index = 0u; index + 1u < path.size(); ++index) {
        if (path[index] == "id") {
            outMessage = "VFX stable IDs are read-only";
            return false;
        }
        const auto child = parent->find(path[index]);
        if (child == parent->end() || !child->is_object()) {
            outMessage = "Unknown VFX property path: " + std::string(propertyPath);
            return false;
        }
        parent = &*child;
    }
    if (path.back() == "id") {
        outMessage = "VFX stable IDs are read-only";
        return false;
    }
    const auto current = parent->find(path.back());
    if (current == parent->end()) {
        outMessage = "Unknown VFX property: " + std::string(propertyPath);
        return false;
    }
    if (!HasCompatibleShape(*current, value)) {
        outMessage = "VFX property value type or shape does not match '" + std::string(propertyPath) + "'";
        return false;
    }

    *current = value;
    if (path.size() == 1u && path.front() == "type" && target->contains("defaultValue") && value.is_string()) {
        VFX::VfxValueType type{};
        if (!TEXT::TryParseEnum(VFX::kVfxValueTypeNames, value.get_ref<const std::string&>(), type)) {
            outMessage = "Unknown VFX input type";
            return false;
        }
        (*target)["defaultValue"] = DefaultInputValue(type);
    }
    if (path.back() == "mode" && value.is_string()) {
        const std::string& mode = value.get_ref<const std::string&>();
        if (mode == "Curve")
            EnsureCurveKeys(*parent, allocateId);
        if (mode == "Gradient")
            EnsureGradientStops(*parent, allocateId);
    }
    outMessage.clear();
    return true;
}

nlohmann::json DescribeVfxSerializedProperties(const nlohmann::json& target) {
    Json properties = Json::array();
    FlattenProperties(target, {}, properties);
    return properties;
}

} // namespace HIKARI::EDITOR
