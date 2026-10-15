#include "Assets/Shader/HIKARI_ShaderParameterSerialization.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

#include "Assets/Shader/Internal/HIKARI_ShaderIdentifier.h"
#include "Assets/Shader/Internal/HIKARI_ShaderJson.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Core/Serialization/Json/HIKARI_TypedValueJson.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

bool IsKnownValueType(BINDING::ValueType type) noexcept { return BINDING::GetValueComponentCount(type) != 0u; }

bool IsValidDisplayText(std::string_view value, bool allowEmpty) noexcept {
    return (allowEmpty || !value.empty()) && value.size() <= kShaderParameterMetadataMaximumBytes &&
           value.find('\0') == std::string_view::npos && TEXT::IsValidUtf8(value);
}

bool ReadText(const nlohmann::json& node, const char* key, std::string& out) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_string()) {
        return false;
    }
    out = found->get<std::string>();
    return out.find('\0') == std::string::npos;
}

bool ReadNumber(const nlohmann::json& node, const char* key, float& out) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number()) {
        return false;
    }
    try {
        out = found->get<float>();
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return MATH::IsFinite(out);
}

bool ReadSlot(const nlohmann::json& node, uint32_t& outSlot) {
    const auto found = node.find("slot");
    if (found == node.end() || !found->is_number_integer()) {
        return false;
    }
    try {
        const int64_t slot = found->get<int64_t>();
        if (slot < 0 || slot >= static_cast<int64_t>(kShaderParameterSlotCount)) {
            return false;
        }
        outSlot = static_cast<uint32_t>(slot);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool ReadValueType(const nlohmann::json& node, BINDING::ValueType& outType) {
    const auto found = node.find("type");
    return found != node.end() && found->is_string() &&
           TEXT::TryParseEnum(BINDING::kValueTypeNames, found->get<std::string>(), outType);
}

void PackValue(const BINDING::Value& value, ShaderParameterSlot& slot) noexcept {
    slot = {};
    switch (value.type) {
    case BINDING::ValueType::Bool: slot[0] = value.boolean ? 1.0f : 0.0f; break;
    case BINDING::ValueType::Int: slot[0] = static_cast<float>(value.integer); break;
    case BINDING::ValueType::Float: slot[0] = value.numeric.x; break;
    case BINDING::ValueType::Float2: slot = {value.numeric.x, value.numeric.y, 0.0f, 0.0f}; break;
    case BINDING::ValueType::Float3: slot = {value.numeric.x, value.numeric.y, value.numeric.z, 0.0f}; break;
    case BINDING::ValueType::Float4:
    case BINDING::ValueType::Color: slot = {value.numeric.x, value.numeric.y, value.numeric.z, value.numeric.w}; break;
    default: break;
    }
}

const ShaderParameterDefinition* FindDefinition(std::span<const ShaderParameterDefinition> definitions,
                                                std::string_view key) noexcept {
    const auto found =
        std::find_if(definitions.begin(), definitions.end(),
                     [key](const ShaderParameterDefinition& definition) { return definition.key == key; });
    return found != definitions.end() ? &*found : nullptr;
}

} // namespace

bool ValidateShaderParameterValue(const ShaderParameterDefinition& definition, const BINDING::Value& value,
                                  BINDING::Value& outCanonical, std::string& outError) {
    if (!IsKnownValueType(definition.defaultValue.type) || value.type != definition.defaultValue.type) {
        outError = "shader parameter value type does not match its definition";
        return false;
    }

    BINDING::Value canonical = BINDING::MakeDefaultValue(value.type);
    switch (value.type) {
    case BINDING::ValueType::Bool: canonical.boolean = value.boolean; break;
    case BINDING::ValueType::Int:
        if (value.integer < -kShaderParameterMaximumExactInteger ||
            value.integer > kShaderParameterMaximumExactInteger) {
            outError = "shader integer parameter exceeds the exact float range";
            return false;
        }
        canonical.integer = value.integer;
        break;
    case BINDING::ValueType::Float: canonical.numeric.x = value.numeric.x; break;
    case BINDING::ValueType::Float2: canonical.numeric = {value.numeric.x, value.numeric.y, 0.0f, 0.0f}; break;
    case BINDING::ValueType::Float3:
        canonical.numeric = {value.numeric.x, value.numeric.y, value.numeric.z, 0.0f};
        break;
    case BINDING::ValueType::Float4:
    case BINDING::ValueType::Color: canonical.numeric = value.numeric; break;
    default: outError = "shader parameter value has an unknown type"; return false;
    }

    if (!BINDING::IsValueFinite(canonical)) {
        outError = "shader parameter value must be finite";
        return false;
    }
    if (definition.hasRange && !BINDING::IsValueWithinRange(canonical, definition.minimum, definition.maximum)) {
        outError = "shader parameter value is outside its declared range";
        return false;
    }

    outCanonical = canonical;
    outError.clear();
    return true;
}

bool ValidateShaderParameterDefinitions(std::span<const ShaderParameterDefinition> definitions, std::string& outError) {
    if (definitions.size() > kShaderParameterSlotCount) {
        outError = "shader parameter count exceeds the 16-slot limit";
        return false;
    }

    for (size_t index = 0u; index < definitions.size(); ++index) {
        const ShaderParameterDefinition& definition = definitions[index];
        if (!INTERNAL::IsIdentifier(definition.key, true) ||
            definition.key.size() > kShaderParameterMetadataMaximumBytes ||
            !IsValidDisplayText(definition.label, false) || !IsValidDisplayText(definition.group, true) ||
            definition.slot >= kShaderParameterSlotCount) {
            outError = "shader parameter identity, text, or slot is invalid";
            return false;
        }
        if (!IsKnownValueType(definition.defaultValue.type) || !MATH::IsFinite(definition.step) ||
            definition.step < 0.0f || (definition.step > 0.0f && !BINDING::IsNumeric(definition.defaultValue.type)) ||
            (definition.hasRange &&
             (!BINDING::IsNumeric(definition.defaultValue.type) || !MATH::IsFinite(definition.minimum) ||
              !MATH::IsFinite(definition.maximum) || definition.minimum > definition.maximum))) {
            outError = "shader parameter type or numeric range is invalid";
            return false;
        }
        if (definition.defaultValue.type == BINDING::ValueType::Int) {
            if ((definition.step > 0.0f && std::trunc(definition.step) != definition.step) ||
                (definition.hasRange &&
                 (std::trunc(definition.minimum) != definition.minimum ||
                  std::trunc(definition.maximum) != definition.maximum ||
                  definition.minimum < -static_cast<float>(kShaderParameterMaximumExactInteger) ||
                  definition.maximum > static_cast<float>(kShaderParameterMaximumExactInteger)))) {
                outError = "integer shader parameter range must use exact integer values";
                return false;
            }
        }

        BINDING::Value canonicalDefault{};
        if (!ValidateShaderParameterValue(definition, definition.defaultValue, canonicalDefault, outError)) {
            return false;
        }
        for (size_t previousIndex = 0u; previousIndex < index; ++previousIndex) {
            const ShaderParameterDefinition& previous = definitions[previousIndex];
            if (previous.key == definition.key || previous.slot == definition.slot) {
                outError = "shader parameter keys and slots must be unique";
                return false;
            }
        }
    }

    outError.clear();
    return true;
}

bool PackShaderParameterValue(const ShaderParameterDefinition& definition, const BINDING::Value& value,
                              ShaderParameterSlot& outSlot, std::string& outError) {
    BINDING::Value canonical{};
    if (!ValidateShaderParameterValue(definition, value, canonical, outError)) {
        return false;
    }
    ShaderParameterSlot slot{};
    PackValue(canonical, slot);
    outSlot = slot;
    outError.clear();
    return true;
}

bool ReadShaderParameterValue(const nlohmann::json& node, BINDING::ValueType type, BINDING::Value& outValue,
                              std::string& outError) {
    if (!IsKnownValueType(type)) {
        outError = "shader parameter declares an unknown value type";
        return false;
    }
    BINDING::Value decoded{};
    const SERIALIZATION::JSON::TypedValueMessages messages{" shader", "shader parameter", "shader parameter"};
    if (!SERIALIZATION::JSON::TryReadTypedValue(node, type, BINDING::MakeDefaultValue(type), decoded, outError,
                                                messages)) {
        return false;
    }
    outValue = decoded;
    outError.clear();
    return true;
}

nlohmann::json WriteShaderParameterValue(const BINDING::Value& value) {
    return SERIALIZATION::JSON::WriteTypedValue(value);
}

bool ParseShaderParameterDefinitions(const nlohmann::json& node, std::vector<ShaderParameterDefinition>& outDefinitions,
                                     std::string& outError) {
    if (!node.is_array() || node.size() > kShaderParameterSlotCount) {
        outError = "shader parameter table must be an array of at most 16 entries";
        return false;
    }

    std::vector<ShaderParameterDefinition> definitions{};
    definitions.reserve(node.size());
    for (const nlohmann::json& item : node) {
        ShaderParameterDefinition definition{};
        BINDING::ValueType type{};
        if (!JSON::HasOnlyKeys(item, {"key", "label", "group", "slot", "type", "default", "min", "max", "step"}) ||
            !ReadText(item, "key", definition.key) || !ReadText(item, "label", definition.label) ||
            !ReadText(item, "group", definition.group) || !ReadSlot(item, definition.slot) ||
            !ReadValueType(item, type)) {
            outError = "shader parameter record is missing a required field or contains an unknown field";
            return false;
        }
        const auto defaultNode = item.find("default");
        if (defaultNode == item.end() ||
            !ReadShaderParameterValue(*defaultNode, type, definition.defaultValue, outError)) {
            if (outError.empty()) {
                outError = "shader parameter default value is missing";
            }
            return false;
        }

        const auto minimum = item.find("min");
        const auto maximum = item.find("max");
        const auto step = item.find("step");
        if ((minimum == item.end()) != (maximum == item.end())) {
            outError = "shader parameter min and max must be specified together";
            return false;
        }
        if (minimum != item.end()) {
            definition.hasRange = true;
            if (!ReadNumber(item, "min", definition.minimum) || !ReadNumber(item, "max", definition.maximum)) {
                outError = "shader parameter min/max must be finite numbers";
                return false;
            }
        }
        if (step != item.end() && !ReadNumber(item, "step", definition.step)) {
            outError = "shader parameter step must be a finite number";
            return false;
        }
        definitions.push_back(std::move(definition));
    }

    if (!ValidateShaderParameterDefinitions(definitions, outError)) {
        return false;
    }
    std::sort(
        definitions.begin(), definitions.end(),
        [](const ShaderParameterDefinition& lhs, const ShaderParameterDefinition& rhs) { return lhs.slot < rhs.slot; });
    outDefinitions = std::move(definitions);
    outError.clear();
    return true;
}

nlohmann::ordered_json WriteShaderParameterDefinitions(std::span<const ShaderParameterDefinition> definitions) {
    std::vector<const ShaderParameterDefinition*> sorted{};
    sorted.reserve(definitions.size());
    for (const ShaderParameterDefinition& definition : definitions) {
        sorted.push_back(&definition);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->slot == rhs->slot ? lhs->key < rhs->key : lhs->slot < rhs->slot;
    });

    nlohmann::ordered_json result = nlohmann::ordered_json::array();
    for (const ShaderParameterDefinition* definition : sorted) {
        nlohmann::ordered_json item{};
        item["key"] = definition->key;
        item["label"] = definition->label;
        item["group"] = definition->group;
        item["slot"] = definition->slot;
        item["type"] = BINDING::ToString(definition->defaultValue.type);
        item["default"] = WriteShaderParameterValue(definition->defaultValue);
        if (definition->hasRange) {
            item["min"] = definition->minimum;
            item["max"] = definition->maximum;
        }
        if (definition->step > 0.0f) {
            item["step"] = definition->step;
        }
        result.push_back(std::move(item));
    }
    return result;
}

bool PackShaderParameterValues(std::span<const ShaderParameterDefinition> definitions,
                               std::span<const ShaderParameterOverride> overrides, ShaderParameterSlots& outSlots,
                               std::string& outError) {
    if (!ValidateShaderParameterDefinitions(definitions, outError)) {
        return false;
    }

    ShaderParameterSlots slots{};
    for (const ShaderParameterDefinition& definition : definitions) {
        if (!PackShaderParameterValue(definition, definition.defaultValue, slots[definition.slot], outError)) {
            return false;
        }
    }
    for (size_t index = 0u; index < overrides.size(); ++index) {
        const ShaderParameterOverride& overrideValue = overrides[index];
        const ShaderParameterDefinition* definition = FindDefinition(definitions, overrideValue.key);
        if (definition == nullptr) {
            outError = "shader parameter override key is not declared";
            return false;
        }
        for (size_t previousIndex = 0u; previousIndex < index; ++previousIndex) {
            if (overrides[previousIndex].key == overrideValue.key) {
                outError = "shader parameter override key is duplicated";
                return false;
            }
        }
        if (!PackShaderParameterValue(*definition, overrideValue.value, slots[definition->slot], outError)) {
            return false;
        }
    }

    outSlots = slots;
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
