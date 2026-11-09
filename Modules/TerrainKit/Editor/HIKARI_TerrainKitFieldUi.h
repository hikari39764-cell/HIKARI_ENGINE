#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "HIKARI/Project/EditorWorkspace.h"

// JSONFIELDS の項目表から窓の欄を作る。資産の読み書きと同じ一つの表を使い、項目ごとの UI を書かない。
// 引擎の Editor/Widgets/HIKARI_JsonFieldsEditor.h のモジュール版で、ImGui の代わりに公開の IEditorWorkspaceUi で描く。
// 配列（Elements）と入れ子は描かない（呼び手が一件ずつ渡す）。custom(key, object) が true を返した項目は呼び手が描く。
namespace HIKARI::TERRAINKIT {

struct NoCustomFieldUi {
    template <class T>
    bool operator()(std::string_view, T&) const {
        return false;
    }
};

template <class T, class Custom>
struct FieldUiDrawer {
    EDITOR_PROJECT::IEditorWorkspaceUi& ui;
    T& object;
    const std::string& prefix; // 欄の ID の頭（同じ表を何度も並べる時に分ける）
    bool& changed;
    const Custom& custom;

    std::string Label(const char* key, const JSONFIELDS::Meta& meta) const {
        std::string label = JSONFIELDS::LabelOf(meta, key);
        if (*meta.unit != '\0') {
            label += " (" + std::string(meta.unit) + ")";
        }
        return label + "##" + prefix;
    }

    template <class Option>
    bool Allowed(const Option& option) const {
        if constexpr (requires { option.visible(object); }) {
            return option.visible(object);
        } else {
            return true;
        }
    }

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::* member, const Options&... options) const {
        const JSONFIELDS::Meta meta = JSONFIELDS::MetaOf(options...);
        if (meta.hidden || meta.readOnly || !(Allowed(options) && ...) || custom(key, object)) {
            return;
        }
        const std::string id = prefix + key;
        M& value = object.*member;
        if constexpr (std::is_same_v<M, bool>) {
            changed |= ui.Checkbox(id, Label(key, meta), &value);
        } else if constexpr (std::is_floating_point_v<M>) {
            EDITOR_PROJECT::EditorWorkspaceFloatInputOptions input{};
            if (meta.hasRange) {
                const double span = meta.range.max - meta.range.min;
                input.step = span > 1000.0 ? 1.0f : (span > 10.0 ? 0.1f : 0.01f);
                input.minimum = static_cast<float>(meta.range.min);
                input.maximum = static_cast<float>(meta.range.max);
                input.clampToRange = 1u;
            }
            auto edited = static_cast<float>(value);
            if (ui.InputFloat(id, Label(key, meta), &edited, input)) {
                value = static_cast<M>(edited);
                changed = true;
            }
        } else if constexpr (std::is_integral_v<M>) {
            EDITOR_PROJECT::EditorWorkspaceIntInputOptions input{};
            constexpr double kLargest = static_cast<double>((std::numeric_limits<int32_t>::max)());
            if (meta.hasRange && meta.range.max <= kLargest) {
                input.minimum = static_cast<int32_t>(meta.range.min);
                input.maximum = static_cast<int32_t>(meta.range.max);
                input.clampToRange = 1u;
            }
            auto edited = static_cast<int32_t>(value);
            if (ui.InputInt(id, Label(key, meta), &edited, input)) {
                value = static_cast<M>(edited);
                changed = true;
            }
        } else if constexpr (std::is_same_v<M, MATH::Vec3>) {
            EDITOR_PROJECT::EditorWorkspaceColor color{value.x, value.y, value.z, 1.0f};
            if (ui.Color4(id, Label(key, meta), &color)) {
                value = MATH::Vec3{color.r, color.g, color.b};
                changed = true;
            }
        } else if constexpr (std::is_same_v<M, std::string>) {
            std::array<char, 256> buffer{};
            value.copy(buffer.data(), buffer.size() - 1u);
            if (ui.InputText(id, Label(key, meta), buffer.data(), buffer.size())) {
                value = buffer.data();
                changed = true;
            }
        }
    }

    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::* member, const TEXT::EnumName<E> (&names)[N],
                    const Options&... options) const {
        const JSONFIELDS::Meta meta = JSONFIELDS::MetaOf(options...);
        if (meta.hidden || meta.readOnly || !(Allowed(options) && ...) || custom(key, object)) {
            return;
        }
        E& value = object.*member;
        ui.Text(JSONFIELDS::LabelOf(meta, key));
        for (const TEXT::EnumName<E>& entry : names) {
            ui.SameLine();
            if (ui.Selectable(prefix + key + entry.name, std::string(entry.name) + "##" + prefix,
                              value == entry.value) &&
                value != entry.value) {
                value = entry.value;
                changed = true;
            }
        }
    }
};

// 表の全項目を描き、どれかが変わったら true。
template <class T, class Fields, class Custom = NoCustomFieldUi>
bool DrawFieldUi(EDITOR_PROJECT::IEditorWorkspaceUi& ui, const std::string& prefix, T& object, const Fields& fields,
                 const Custom& custom = {}) {
    bool changed = false;
    fields(FieldUiDrawer<T, Custom>{ui, object, prefix, changed, custom});
    return changed;
}

} // namespace HIKARI::TERRAINKIT
