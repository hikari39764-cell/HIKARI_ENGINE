#pragma once

#include "HIKARI/Project/ValueTypes.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"

// SDK 値と実行時値の変換は Inspector と WorldView で共有する。
namespace HIKARI::PROJECT::DETAIL {
    inline BINDING::ValueType ToRuntimeBindingType(
        GameProjectBindingValueType type) noexcept {
        switch (type) {
        case GameProjectBindingValueType::Bool:
            return BINDING::ValueType::Bool;
        case GameProjectBindingValueType::Int:
            return BINDING::ValueType::Int;
        case GameProjectBindingValueType::Float2:
            return BINDING::ValueType::Float2;
        case GameProjectBindingValueType::Float3:
            return BINDING::ValueType::Float3;
        case GameProjectBindingValueType::Float4:
            return BINDING::ValueType::Float4;
        case GameProjectBindingValueType::Color:
            return BINDING::ValueType::Color;
        case GameProjectBindingValueType::Float:
        default:
            return BINDING::ValueType::Float;
        }
    }

    inline GameProjectBindingValueType ToProjectBindingType(
        BINDING::ValueType type) noexcept {
        switch (type) {
        case BINDING::ValueType::Bool:
            return GameProjectBindingValueType::Bool;
        case BINDING::ValueType::Int:
            return GameProjectBindingValueType::Int;
        case BINDING::ValueType::Float2:
            return GameProjectBindingValueType::Float2;
        case BINDING::ValueType::Float3:
            return GameProjectBindingValueType::Float3;
        case BINDING::ValueType::Float4:
            return GameProjectBindingValueType::Float4;
        case BINDING::ValueType::Color:
            return GameProjectBindingValueType::Color;
        case BINDING::ValueType::Float:
        default:
            return GameProjectBindingValueType::Float;
        }
    }

    inline BINDING::Value ToRuntimeValue(
        const GameProjectBindingValue& value) noexcept {
        BINDING::Value result{};
        result.type = ToRuntimeBindingType(value.type);
        result.boolean = value.boolean;
        result.integer = value.integer;
        result.numeric = {
            value.numeric[0],
            value.numeric[1],
            value.numeric[2],
            value.numeric[3]
        };
        return result;
    }

    inline GameProjectBindingValue ToProjectValue(
        const BINDING::Value& value) noexcept {
        GameProjectBindingValue result{};
        result.type = ToProjectBindingType(value.type);
        result.boolean = value.boolean;
        result.integer = value.integer;
        result.numeric[0] = value.numeric.x;
        result.numeric[1] = value.numeric.y;
        result.numeric[2] = value.numeric.z;
        result.numeric[3] = value.numeric.w;
        return result;
    }

}
