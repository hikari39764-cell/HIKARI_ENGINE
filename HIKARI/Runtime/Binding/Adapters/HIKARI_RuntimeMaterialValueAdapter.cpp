#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"

namespace HIKARI::BINDING::MATERIAL_ADAPTER {

std::optional<MaterialValueType> ToMaterialValueType(ValueType type) noexcept {
    switch (type) {
    case ValueType::Bool:
        // Material graphs expose shader scalars rather than a separate
        // boolean socket. Normalize bool to 0/1 Float1 so it can drive
        // Select conditions, masks, and ordinary scalar inputs.
        return MaterialValueType::Float1;
    case ValueType::Float: return MaterialValueType::Float1;
    case ValueType::Float2: return MaterialValueType::Float2;
    case ValueType::Float3: return MaterialValueType::Float3;
    case ValueType::Float4:
    case ValueType::Color: return MaterialValueType::Float4;
    case ValueType::Int:
        // Int-to-float is intentionally not implicit. Large gameplay
        // integers cannot be represented exactly in shader float slots.
        return std::nullopt;
    }
    return std::nullopt;
}

bool TryConvertValue(const Value& source, MaterialValueType expectedType, MaterialValueData& outValue) noexcept {
    const std::optional<MaterialValueType> materialType = ToMaterialValueType(source.type);
    if (!materialType.has_value() || *materialType != expectedType) {
        return false;
    }
    MaterialValueData converted{};
    converted.type = expectedType;
    if (source.type == ValueType::Bool) {
        converted.numeric.x = source.boolean ? 1.0f : 0.0f;
    } else {
        converted.numeric = {source.numeric.x, source.numeric.y, source.numeric.z, source.numeric.w};
    }
    outValue = converted;
    return true;
}

} // namespace HIKARI::BINDING::MATERIAL_ADAPTER
