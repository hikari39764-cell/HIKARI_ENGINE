#pragma once

#include <cstdint>
#include <string_view>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::BINDING {

enum class ValueType : uint8_t {
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Color,
};

inline constexpr TEXT::EnumName<ValueType> kValueTypeNames[] = {
    {ValueType::Bool, "Bool"},     {ValueType::Int, "Int"},       {ValueType::Float, "Float"},
    {ValueType::Float2, "Float2"}, {ValueType::Float3, "Float3"}, {ValueType::Float4, "Float4"},
    {ValueType::Color, "Color"},
};

// 全ての値型。型を問わない変数選択（検査器の VariableRef など）が使う。
inline constexpr auto kAllValueTypes = TEXT::EnumValueList(kValueTypeNames);

struct NumericValue {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    bool operator==(const NumericValue&) const noexcept = default;
};

struct Value {
    ValueType type = ValueType::Float;
    bool boolean = false;
    int64_t integer = 0;
    NumericValue numeric{};

    bool operator==(const Value&) const noexcept = default;
};

std::string_view ToString(ValueType type) noexcept;
uint32_t GetValueComponentCount(ValueType type) noexcept;
bool IsNumeric(ValueType type) noexcept;
bool IsValueCompatible(const Value& value, ValueType expectedType) noexcept;
bool IsValueFinite(const Value& value) noexcept;
bool IsValueWithinRange(const Value& value, float minimum, float maximum) noexcept;
Value MakeDefaultValue(ValueType type) noexcept;

} // namespace HIKARI::BINDING
