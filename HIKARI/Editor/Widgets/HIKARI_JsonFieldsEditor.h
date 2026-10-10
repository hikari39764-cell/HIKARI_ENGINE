#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <imgui.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_NumberField.h"

// JSONFIELDS の項目表から設定の編集 UI を作る。資産の読み書きと同じ一つの表を使い、項目ごとの UI を書かない。
// 表示名は "<labelPrefix>.<key>" の翻訳、無ければ key を語に分けたもの。
// 資産の参照など表だけでは描けない項目は、custom(key, object, result) が true を返して自分で描く。
// 数値は NumberField（右クリックで T{} の既定値へ戻せる）。JSONFIELDS::Slider の項目は滑块、他はドラッグ欄。
// "enabled" が false なら、その後の項目は灰色にする。
namespace HIKARI::EDITOR {

// "textureIlluminance" -> "Texture Illuminance"
std::string HumanizeFieldKey(std::string_view key);
std::string ResolveFieldLabel(std::string_view labelPrefix, std::string_view key);
// 0..1 の色らしい項目（color / albedo / tint）を色の選択で表示する。
bool IsColorFieldKey(std::string_view key) noexcept;

struct FieldEditResult {
    bool changed = false;
    bool active = false;
};

namespace DETAIL {

struct NoCustomField {
    template <class T>
    bool operator()(std::string_view, T&, FieldEditResult&) const {
        return false;
    }
};

template <class T, class Custom>
struct FieldDrawer {
    T& object;
    std::string_view labelPrefix;
    FieldEditResult& result;
    const Custom& custom;
    // "enabled" を切った後の項目（効かない値）を灰色にする。
    mutable bool inactive = false;

    void Track(bool changed) const {
        result.changed |= changed;
        result.active |= ImGui::IsItemActive();
    }

    // 既定値は構造体の初期値（T{}）。右クリックの「既定値へ戻す」に使う。
    template <class M>
    static const M* DefaultOf(M T::* member) {
        if constexpr (std::is_default_constructible_v<T>) {
            static const T defaults{};
            return &(defaults.*member);
        } else {
            return nullptr;
        }
    }

    template <class M, class... Options>
    void operator()(const char* key, M T::* member, const Options&... options) const {
        if (custom(key, object, result)) {
            return;
        }
        const std::string label = ResolveFieldLabel(labelPrefix, key);
        ImGui::BeginDisabled(inactive);
        InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            ImGui::EndDisabled();
            return;
        }
        ImGui::PushID(key);
        M& value = object.*member;
        NumberRange range{};
        bool slider = false;
        ((void)ReadRange(options, range, slider), ...);
        const M* fallback = DefaultOf(member);
        if constexpr (std::is_same_v<M, bool>) {
            Track(ImGui::Checkbox("##value", &value));
        } else if constexpr (std::is_arithmetic_v<M>) {
            Track(slider ? SliderNumber("##value", value, range.minimum, range.maximum, fallback)
                         : DragNumber("##value", value, range, fallback));
        } else if constexpr (std::is_same_v<M, MATH::Vec2>) {
            Track(DragNumbers("##value", &value.x, 2, range, fallback != nullptr ? &fallback->x : nullptr));
        } else if constexpr (std::is_same_v<M, MATH::Vec3>) {
            if (IsColorFieldKey(key) && range.maximum <= 16.0) {
                Track(
                    ImGui::ColorEdit3("##value", &value.x,
                                      ImGuiColorEditFlags_Float | (range.maximum > 1.0 ? ImGuiColorEditFlags_HDR : 0)));
            } else {
                Track(DragNumbers("##value", &value.x, 3, range, fallback != nullptr ? &fallback->x : nullptr));
            }
        } else if constexpr (std::is_same_v<M, std::string>) {
            char buffer[256]{};
            value.copy(buffer, sizeof(buffer) - 1u);
            if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                value = buffer;
                Track(true);
            } else {
                Track(false);
            }
        }
        ImGui::PopID();
        ImGui::EndDisabled();
        if constexpr (std::is_same_v<M, bool>) {
            inactive |= std::string_view(key) == "enabled" && !value;
        }
    }

    template <class E, std::size_t N, class... Options>
    void operator()(const char* key, E T::* member, const TEXT::EnumName<E> (&names)[N], const Options&...) const {
        if (custom(key, object, result)) {
            return;
        }
        const std::string label = ResolveFieldLabel(labelPrefix, key);
        InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return;
        }
        ImGui::BeginDisabled(inactive);
        ImGui::PushID(key);
        E& value = object.*member;
        const std::string current = ResolveFieldLabel(labelPrefix, TEXT::EnumToName(names, value));
        if (ImGui::BeginCombo("##value", current.c_str())) {
            for (const TEXT::EnumName<E>& entry : names) {
                const bool selected = entry.value == value;
                if (ImGui::Selectable(ResolveFieldLabel(labelPrefix, entry.name).c_str(), selected) && !selected) {
                    value = entry.value;
                    result.changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ImGui::EndDisabled();
    }

    template <class Option>
    static bool ReadRange(const Option& option, NumberRange& range, bool& slider) {
        if constexpr (std::is_same_v<Option, JSONFIELDS::Range>) {
            range.minimum = option.min;
            range.maximum = option.max;
            slider = option.slider;
        }
        return true;
    }
};

} // namespace DETAIL

// 表の全項目を描く。changed は値が変わったフレーム、active はドラッグなどの編集が続いている間。
template <class T, class Fields, class Custom = DETAIL::NoCustomField>
FieldEditResult DrawJsonFields(std::string_view labelPrefix, T& object, const Fields& fields,
                               const Custom& custom = {}) {
    FieldEditResult result{};
    fields(DETAIL::FieldDrawer<T, Custom>{object, labelPrefix, result, custom});
    return result;
}

} // namespace HIKARI::EDITOR

#endif
