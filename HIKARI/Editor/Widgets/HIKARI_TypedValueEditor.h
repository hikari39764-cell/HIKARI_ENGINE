#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

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
