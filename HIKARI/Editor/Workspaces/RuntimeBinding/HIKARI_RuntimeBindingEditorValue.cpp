#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    bool TryParseRuntimeBindingEditorValueType(
        std::string_view text,
        BINDING::ValueType& outType) noexcept {

        std::string lowered(text);
        std::transform(
            lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
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

    bool TryReadRuntimeBindingEditorValue(
        const nlohmann::json& node,
        BINDING::ValueType expectedType,
        BINDING::Value& outValue,
        std::string& outMessage) {

        BINDING::Value value =
            BINDING::MakeDefaultValue(expectedType);
        try {
            switch (expectedType) {
            case BINDING::ValueType::Bool:
                if (!node.is_boolean()) {
                    outMessage = "Expected a boolean value";
                    return false;
                }
                value.boolean = node.get<bool>();
                break;
            case BINDING::ValueType::Int:
                if (!node.is_number_integer()) {
                    outMessage = "Expected an integer value";
                    return false;
                }
                value.integer = node.get<int64_t>();
                break;
            case BINDING::ValueType::Float:
                if (!node.is_number()) {
                    outMessage = "Expected a numeric value";
                    return false;
                }
                value.numeric.x = node.get<float>();
                break;
            case BINDING::ValueType::Float2:
            case BINDING::ValueType::Float3:
            case BINDING::ValueType::Float4:
            case BINDING::ValueType::Color: {
                const size_t componentCount =
                    BINDING::GetValueComponentCount(expectedType);
                if (!node.is_array() || node.size() != componentCount) {
                    outMessage = "Expected a numeric array with " +
                        std::to_string(componentCount) + " components";
                    return false;
                }
                float* components[] = {
                    &value.numeric.x,
                    &value.numeric.y,
                    &value.numeric.z,
                    &value.numeric.w,
                };
                for (size_t index = 0u;
                    index < componentCount; ++index) {
                    if (!node[index].is_number()) {
                        outMessage =
                            "Runtime binding value components must be numeric";
                        return false;
                    }
                    *components[index] = node[index].get<float>();
                }
                break;
            }
            }
        } catch (const nlohmann::json::exception& exception) {
            outMessage = "Invalid runtime binding value: " +
                std::string(exception.what());
            return false;
        }

        outValue = value;
        outMessage.clear();
        return true;
    }

    nlohmann::json WriteRuntimeBindingEditorValue(
        const BINDING::Value& value) {

        switch (value.type) {
        case BINDING::ValueType::Bool:
            return value.boolean;
        case BINDING::ValueType::Int:
            return value.integer;
        case BINDING::ValueType::Float:
            return value.numeric.x;
        case BINDING::ValueType::Float2:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
            });
        case BINDING::ValueType::Float3:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
                value.numeric.z,
            });
        case BINDING::ValueType::Float4:
        case BINDING::ValueType::Color:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
                value.numeric.z,
                value.numeric.w,
            });
        }
        return nullptr;
    }

#if defined(HIKARI_WITH_EDITOR)
    RuntimeBindingValueEditResult DrawRuntimeBindingValueEditor(
        const char* id,
        BINDING::Value& value,
        const BINDING::VariableDefinition* range,
        float width) {

        RuntimeBindingValueEditResult result{};
        ImGui::SetNextItemWidth(width);
        const float speed = range != nullptr
            ? std::max(range->step, 0.0001f)
            : 0.01f;
        const float minimum = range != nullptr && range->hasRange
            ? range->minimum
            : 0.0f;
        const float maximum = range != nullptr && range->hasRange
            ? range->maximum
            : 0.0f;
        const float* minimumPointer =
            range != nullptr && range->hasRange ? &minimum : nullptr;
        const float* maximumPointer =
            range != nullptr && range->hasRange ? &maximum : nullptr;

        switch (value.type) {
        case BINDING::ValueType::Bool:
            result.changed = ImGui::Checkbox(id, &value.boolean);
            result.committed = result.changed;
            break;
        case BINDING::ValueType::Int: {
            int64_t integerMinimum =
                static_cast<int64_t>(std::floor(minimum));
            int64_t integerMaximum =
                static_cast<int64_t>(std::ceil(maximum));
            result.changed = ImGui::DragScalar(
                id,
                ImGuiDataType_S64,
                &value.integer,
                std::max(speed, 1.0f),
                minimumPointer != nullptr ? &integerMinimum : nullptr,
                maximumPointer != nullptr ? &integerMaximum : nullptr);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case BINDING::ValueType::Float:
            result.changed = ImGui::DragFloat(
                id,
                &value.numeric.x,
                speed,
                minimum,
                maximum);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case BINDING::ValueType::Float2:
            result.changed = ImGui::DragFloat2(
                id,
                &value.numeric.x,
                speed,
                minimum,
                maximum);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case BINDING::ValueType::Float3:
            result.changed = ImGui::DragFloat3(
                id,
                &value.numeric.x,
                speed,
                minimum,
                maximum);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case BINDING::ValueType::Float4:
            result.changed = ImGui::DragFloat4(
                id,
                &value.numeric.x,
                speed,
                minimum,
                maximum);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case BINDING::ValueType::Color:
            result.changed = ImGui::ColorEdit4(
                id,
                &value.numeric.x,
                ImGuiColorEditFlags_Float);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        return result;
    }
#endif

} // namespace HIKARI::EDITOR
