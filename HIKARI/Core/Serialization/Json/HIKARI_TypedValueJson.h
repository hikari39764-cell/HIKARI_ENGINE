#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

namespace HIKARI::SERIALIZATION::JSON {

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
nlohmann::json WriteTypedValue(const Value& value) {
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
bool TryReadTypedValue(const nlohmann::json& node, Type expectedType, Value value, Value& outValue,
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
            if (node.is_number_unsigned() &&
                node.get<uint64_t>() > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                return fail("Integer" + q + " value exceeds the signed 64-bit range");
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

} // namespace HIKARI::SERIALIZATION::JSON
