#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

// Bool/Int/Float/Float2/Float3/Float4/Color を持つ値型（BINDING::Value, VFX::VfxValue）の
// JSON 読み書きと編集 UI。値型は type/boolean/integer/numeric を持つ。
namespace HIKARI::EDITOR {

// 診断文の言い回し。例: {" VFX", "VFX", "VFX"} → "Expected a boolean VFX value"
struct TypedValueMessages {
    std::string_view typeQualifier;
    std::string_view subject;
    std::string_view subjectLower;
};

template <class Type>
constexpr size_t TypedValueComponentCount(Type type) noexcept {
    switch (type) {
    case Type::Float2: return 2u;
    case Type::Float3: return 3u;
    case Type::Float4:
    case Type::Color: return 4u;
    default: return 1u;
    }
}

template <class Value>
nlohmann::json WriteTypedEditorValue(const Value& value) {
    using Type = decltype(value.type);
    const auto& n = value.numeric;
    switch (value.type) {
    case Type::Bool: return value.boolean;
    case Type::Int: return value.integer;
    case Type::Float: return n.x;
    case Type::Float2: return nlohmann::json::array({n.x, n.y});
    case Type::Float3: return nlohmann::json::array({n.x, n.y, n.z});
    case Type::Float4:
    case Type::Color: return nlohmann::json::array({n.x, n.y, n.z, n.w});
    }
    return nullptr;
}

// value は expectedType の既定値で渡す。失敗時は outValue を変更しない。
template <class Value, class Type>
bool TryReadTypedEditorValue(const nlohmann::json& node, Type expectedType, Value value, Value& outValue,
                             std::string& outMessage, const TypedValueMessages& text) {
    const auto fail = [&](std::string message) {
        outMessage = std::move(message);
        return false;
    };
    const std::string q(text.typeQualifier);
    try {
        switch (expectedType) {
        case Type::Bool:
            if (!node.is_boolean()) {
                return fail("Expected a boolean" + q + " value");
            }
            value.boolean = node.get<bool>();
            break;
        case Type::Int:
            if (!node.is_number_integer()) {
                return fail("Expected an integer" + q + " value");
            }
            value.integer = node.get<int64_t>();
            break;
        case Type::Float:
            if (!node.is_number()) {
                return fail("Expected a numeric" + q + " value");
            }
            value.numeric.x = node.get<float>();
            break;
        case Type::Float2:
        case Type::Float3:
        case Type::Float4:
        case Type::Color: {
            const size_t count = TypedValueComponentCount(expectedType);
            if (!node.is_array() || node.size() != count) {
                return fail("Expected a" + q + " numeric array with " + std::to_string(count) + " components");
            }
            float* components[] = {&value.numeric.x, &value.numeric.y, &value.numeric.z, &value.numeric.w};
            for (size_t index = 0u; index < count; ++index) {
                if (!node[index].is_number()) {
                    return fail(std::string(text.subject) + " value components must be numeric");
                }
                *components[index] = node[index].get<float>();
            }
            break;
        }
        }
    } catch (const nlohmann::json::exception& exception) {
        return fail("Invalid " + std::string(text.subjectLower) + " value: " + exception.what());
    }
    outValue = value;
    outMessage.clear();
    return true;
}

#if defined(HIKARI_WITH_EDITOR)
struct TypedValueEditResult {
    bool changed = false;
    bool committed = false;
};

struct TypedValueRange {
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.01f;
};

template <class Value>
TypedValueEditResult DrawTypedEditorValue(const char* id, Value& value, const TypedValueRange& range, float width) {
    using Type = decltype(value.type);
    TypedValueEditResult result{};
    ImGui::SetNextItemWidth(width);
    const float minimum = range.hasRange ? range.minimum : 0.0f;
    const float maximum = range.hasRange ? range.maximum : 0.0f;
    float* numeric = &value.numeric.x;
    switch (value.type) {
    case Type::Bool:
        result.changed = ImGui::Checkbox(id, &value.boolean);
        result.committed = result.changed;
        return result;
    case Type::Int: {
        int64_t integerMinimum = static_cast<int64_t>(std::floor(minimum));
        int64_t integerMaximum = static_cast<int64_t>(std::ceil(maximum));
        result.changed =
            ImGui::DragScalar(id, ImGuiDataType_S64, &value.integer, (std::max)(range.step, 1.0f),
                              range.hasRange ? &integerMinimum : nullptr, range.hasRange ? &integerMaximum : nullptr);
        break;
    }
    case Type::Float: result.changed = ImGui::DragFloat(id, numeric, range.step, minimum, maximum); break;
    case Type::Float2: result.changed = ImGui::DragFloat2(id, numeric, range.step, minimum, maximum); break;
    case Type::Float3: result.changed = ImGui::DragFloat3(id, numeric, range.step, minimum, maximum); break;
    case Type::Float4: result.changed = ImGui::DragFloat4(id, numeric, range.step, minimum, maximum); break;
    case Type::Color: result.changed = ImGui::ColorEdit4(id, numeric, ImGuiColorEditFlags_Float); break;
    }
    result.committed = ImGui::IsItemDeactivatedAfterEdit();
    return result;
}
#endif

} // namespace HIKARI::EDITOR
