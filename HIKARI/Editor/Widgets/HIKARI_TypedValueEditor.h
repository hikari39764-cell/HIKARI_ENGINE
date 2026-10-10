#pragma once

#include <cstdint>

#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Widgets/HIKARI_NumberField.h"
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
    const NumberRange numberRange{.minimum = range.hasRange ? range.minimum : 0.0,
                                  .maximum = range.hasRange ? range.maximum : 0.0,
                                  .step = range.step};
    float* numeric = &value.numeric.x;
    switch (value.type) {
    case Type::Bool:
        result.changed = ImGui::Checkbox(id, &value.boolean);
        result.committed = result.changed;
        return result;
    case Type::Int: result.changed = DragNumber(id, value.integer, numberRange); break;
    case Type::Float: result.changed = DragNumber(id, *numeric, numberRange); break;
    case Type::Float2: result.changed = DragNumbers(id, numeric, 2, numberRange); break;
    case Type::Float3: result.changed = DragNumbers(id, numeric, 3, numberRange); break;
    case Type::Float4: result.changed = DragNumbers(id, numeric, 4, numberRange); break;
    case Type::Color: result.changed = ImGui::ColorEdit4(id, numeric, ImGuiColorEditFlags_Float); break;
    }
    result.committed = ImGui::IsItemDeactivatedAfterEdit();
    return result;
}
#endif

} // namespace HIKARI::EDITOR
