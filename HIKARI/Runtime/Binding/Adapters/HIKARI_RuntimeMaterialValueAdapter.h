#pragma once

#include <optional>

#include "Assets/Material/Graph/HIKARI_MaterialGraphData.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"

namespace HIKARI::BINDING::MATERIAL_ADAPTER {

    std::optional<MaterialValueType> ToMaterialValueType(
        ValueType type) noexcept;

    bool TryConvertValue(
        const Value& source,
        MaterialValueType expectedType,
        MaterialValueData& outValue) noexcept;

} // namespace HIKARI::BINDING::MATERIAL_ADAPTER
