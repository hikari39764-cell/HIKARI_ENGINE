#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"

#include <cmath>

namespace HIKARI::BINDING {

    std::string_view ToString(ValueType type) noexcept {
        switch (type) {
        case ValueType::Bool: return "Bool";
        case ValueType::Int: return "Int";
        case ValueType::Float: return "Float";
        case ValueType::Float2: return "Float2";
        case ValueType::Float3: return "Float3";
        case ValueType::Float4: return "Float4";
        case ValueType::Color: return "Color";
        default: return "Float";
        }
    }

    bool TryParseValueType(
        std::string_view text,
        ValueType& outType) noexcept {

        if (text == "Bool") {
            outType = ValueType::Bool;
        } else if (text == "Int") {
            outType = ValueType::Int;
        } else if (text == "Float") {
            outType = ValueType::Float;
        } else if (text == "Float2") {
            outType = ValueType::Float2;
        } else if (text == "Float3") {
            outType = ValueType::Float3;
        } else if (text == "Float4") {
            outType = ValueType::Float4;
        } else if (text == "Color") {
            outType = ValueType::Color;
        } else {
            return false;
        }
        return true;
    }

    uint32_t GetValueComponentCount(ValueType type) noexcept {
        switch (type) {
        case ValueType::Bool:
        case ValueType::Int:
        case ValueType::Float:
            return 1u;
        case ValueType::Float2:
            return 2u;
        case ValueType::Float3:
            return 3u;
        case ValueType::Float4:
        case ValueType::Color:
            return 4u;
        default:
            return 0u;
        }
    }

    bool IsNumeric(ValueType type) noexcept {
        return type != ValueType::Bool;
    }

    bool IsValueCompatible(
        const Value& value,
        ValueType expectedType) noexcept {

        return value.type == expectedType;
    }

    bool IsValueFinite(const Value& value) noexcept {
        switch (value.type) {
        case ValueType::Bool:
        case ValueType::Int:
            return true;
        case ValueType::Float:
        case ValueType::Float2:
        case ValueType::Float3:
        case ValueType::Float4:
        case ValueType::Color:
            return std::isfinite(value.numeric.x) &&
                std::isfinite(value.numeric.y) &&
                std::isfinite(value.numeric.z) &&
                std::isfinite(value.numeric.w);
        default:
            return false;
        }
    }

    bool IsValueWithinRange(
        const Value& value,
        float minimum,
        float maximum) noexcept {

        if (!IsNumeric(value.type) ||
            !IsValueFinite(value) ||
            !std::isfinite(minimum) ||
            !std::isfinite(maximum) ||
            minimum > maximum) {
            return false;
        }
        if (value.type == ValueType::Int) {
            const double integer = static_cast<double>(value.integer);
            return integer >= static_cast<double>(minimum) &&
                integer <= static_cast<double>(maximum);
        }

        const float components[] = {
            value.numeric.x,
            value.numeric.y,
            value.numeric.z,
            value.numeric.w
        };
        const uint32_t componentCount =
            GetValueComponentCount(value.type);
        for (uint32_t index = 0u;
            index < componentCount;
            ++index) {
            if (components[index] < minimum ||
                components[index] > maximum) {
                return false;
            }
        }
        return true;
    }

    Value MakeDefaultValue(ValueType type) noexcept {
        Value value{};
        value.type = type;
        if (type == ValueType::Color) {
            value.numeric = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        return value;
    }

} // namespace HIKARI::BINDING
