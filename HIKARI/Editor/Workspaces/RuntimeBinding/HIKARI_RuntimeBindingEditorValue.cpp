#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"

#include <algorithm>
#include <cctype>

#include "Editor/Widgets/HIKARI_TypedValueEditor.h"
#include "Core/Serialization/Json/HIKARI_TypedValueJson.h"

namespace HIKARI::EDITOR {

bool TryParseRuntimeBindingEditorValueType(std::string_view text, BINDING::ValueType& outType) noexcept {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (lowered == "bool" || lowered == "boolean") {
        outType = BINDING::ValueType::Bool;
    } else if (lowered == "int" || lowered == "integer") {
        outType = BINDING::ValueType::Int;
    } else if (lowered == "float") {
        outType = BINDING::ValueType::Float;
    } else if (lowered == "float2" || lowered == "vec2") {
        outType = BINDING::ValueType::Float2;
    } else if (lowered == "float3" || lowered == "vec3") {
        outType = BINDING::ValueType::Float3;
    } else if (lowered == "float4" || lowered == "vec4") {
        outType = BINDING::ValueType::Float4;
    } else if (lowered == "color" || lowered == "colour") {
        outType = BINDING::ValueType::Color;
    } else {
        return false;
    }
    return true;
}

namespace {
constexpr SERIALIZATION::JSON::TypedValueMessages kMessages{"", "Runtime binding", "runtime binding"};
} // namespace

bool TryReadRuntimeBindingEditorValue(const nlohmann::json& node, BINDING::ValueType expectedType,
                                      BINDING::Value& outValue, std::string& outMessage) {
    return SERIALIZATION::JSON::TryReadTypedValue(node, expectedType, BINDING::MakeDefaultValue(expectedType), outValue,
                                                  outMessage, kMessages);
}

nlohmann::json WriteRuntimeBindingEditorValue(const BINDING::Value& value) {
    return SERIALIZATION::JSON::WriteTypedValue(value);
}

#if defined(HIKARI_WITH_EDITOR)
RuntimeBindingValueEditResult DrawRuntimeBindingValueEditor(const char* id, BINDING::Value& value,
                                                            const BINDING::VariableDefinition* range, float width) {
    TypedValueRange limits{};
    if (range != nullptr) {
        limits = {range->hasRange, range->minimum, range->maximum, std::max(range->step, 0.0001f)};
    }
    return DrawTypedEditorValue(id, value, limits, width);
}
#endif

} // namespace HIKARI::EDITOR
