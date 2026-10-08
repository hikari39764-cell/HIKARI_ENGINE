#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <imgui.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"

// JSONFIELDS の項目表から設定の編集 UI を作る。資産の読み書きと同じ一つの表を使い、項目ごとの UI を書かない。
// 表示名は "<labelPrefix>.<key>" の翻訳、無ければ key を語に分けたもの。
// 資産の参照など表だけでは描けない項目は、custom(key, object, result) が true を返して自分で描く。
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

    void Track(bool changed) const {
        result.changed |= changed;
        result.active |= ImGui::IsItemActive();
    }

    template <class M, class... Options>
    void operator()(const char* key, M T::* member, const Options&... options) const {
        if (custom(key, object, result)) {
            return;
        }
        const std::string label = ResolveFieldLabel(labelPrefix, key);
        InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return;
        }
        ImGui::PushID(key);
        M& value = object.*member;
        double minimum = 0.0;
        double maximum = 0.0;
        bool ranged = false;
        ((void)ReadRange(options, minimum, maximum, ranged), ...);
        const float speed = DragSpeed(Magnitude(value), minimum, maximum, ranged);
        if constexpr (std::is_same_v<M, bool>) {
            Track(ImGui::Checkbox("##value", &value));
        } else if constexpr (std::is_same_v<M, float>) {
            Track(ImGui::DragFloat("##value", &value, speed, static_cast<float>(minimum), static_cast<float>(maximum),
                                   "%.4g", ranged ? ImGuiSliderFlags_AlwaysClamp : 0));
        } else if constexpr (std::is_same_v<M, double>) {
            Track(ImGui::DragScalar("##value", ImGuiDataType_Double, &value, speed, ranged ? &minimum : nullptr,
                                    ranged ? &maximum : nullptr, "%.4f", ranged ? ImGuiSliderFlags_AlwaysClamp : 0));
        } else if constexpr (std::is_same_v<M, uint32_t>) {
            const uint32_t low = static_cast<uint32_t>(minimum);
            const uint32_t high = static_cast<uint32_t>(maximum);
            Track(ImGui::DragScalar("##value", ImGuiDataType_U32, &value, 1.0f, ranged ? &low : nullptr,
                                    ranged ? &high : nullptr, "%u", ranged ? ImGuiSliderFlags_AlwaysClamp : 0));
        } else if constexpr (std::is_same_v<M, int32_t> || std::is_same_v<M, int>) {
            Track(ImGui::DragInt("##value", &value, 1.0f, static_cast<int>(minimum), static_cast<int>(maximum), "%d",
                                 ranged ? ImGuiSliderFlags_AlwaysClamp : 0));
        } else if constexpr (std::is_same_v<M, MATH::Vec2>) {
            Track(ImGui::DragFloat2("##value", &value.x, speed, static_cast<float>(minimum),
                                    static_cast<float>(maximum)));
        } else if constexpr (std::is_same_v<M, MATH::Vec3>) {
            if (IsColorFieldKey(key) && maximum <= 16.0) {
                Track(ImGui::ColorEdit3("##value", &value.x,
                                        ImGuiColorEditFlags_Float | (maximum > 1.0 ? ImGuiColorEditFlags_HDR : 0)));
            } else {
                Track(ImGui::DragFloat3("##value", &value.x, speed, static_cast<float>(minimum),
                                        static_cast<float>(maximum), "%.4g"));
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
        ImGui::PushID(key);
        E& value = object.*member;
        const char* current = TEXT::EnumToName(names, value);
        if (ImGui::BeginCombo("##value", current)) {
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
    }

    // 1 画素あたりの変化量。値の大きさの 1% を基本に、範囲の 1/2000 から 1/500 の間へ収める。
    static float DragSpeed(double magnitude, double minimum, double maximum, bool ranged) {
        const double relative = magnitude * 0.01;
        if (!ranged) {
            return static_cast<float>(relative > 0.001 ? relative : 0.001);
        }
        const double span = maximum - minimum;
        const double low = span / 2000.0;
        const double high = span / 500.0;
        return static_cast<float>(relative < low ? low : (relative > high ? high : relative));
    }

    template <class M>
    static double Magnitude(const M& value) {
        if constexpr (std::is_same_v<M, MATH::Vec2>) {
            return std::max(std::abs(value.x), std::abs(value.y));
        } else if constexpr (std::is_same_v<M, MATH::Vec3>) {
            return std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
        } else if constexpr (std::is_arithmetic_v<M>) {
            return std::abs(static_cast<double>(value));
        } else {
            return 0.0;
        }
    }

    template <class Option>
    static bool ReadRange(const Option& option, double& minimum, double& maximum, bool& ranged) {
        if constexpr (std::is_same_v<Option, JSONFIELDS::Range>) {
            minimum = option.min;
            maximum = option.max;
            ranged = true;
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
