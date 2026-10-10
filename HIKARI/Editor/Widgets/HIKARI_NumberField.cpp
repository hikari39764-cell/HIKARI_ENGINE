#if defined(HIKARI_WITH_EDITOR)

#include "Editor/Widgets/HIKARI_NumberField.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <imgui.h>

#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR {
namespace {

// ドラッグ中の欄の速さ。同時に動かせる欄は一つなので一つだけ覚える。
struct DragLatch {
    ImGuiID id = 0;
    double speed = 0.0;
};
DragLatch g_dragLatch;

bool IsRanged(const NumberRange& range) { return range.minimum < range.maximum; }

double DragSpeed(double value, const NumberRange& range, bool integer) {
    const bool ranged = IsRanged(range);
    const double span = range.maximum - range.minimum;
    const double base = range.step > 0.0 ? range.step : (ranged ? span / 1000.0 : 0.001);
    // 比例する値は 100 画素で約 5 割変わる（0.0005 の霧も 128000 lux の太陽も同じ手応え）。0 からは base で動き出し、
    // 極小の値でも止まらないよう base の 1/1000 を下限にする。0 をまたぐ範囲（オフセットや角度）は比例させない。
    const bool proportional = range.proportional && !(ranged && range.minimum < 0.0) && value != 0.0;
    double speed = proportional ? (std::max)(std::abs(value) * 0.005, base * 0.001) : base;
    if (integer) {
        speed = (std::max)(speed, 0.05);
    }
    // 範囲があれば、全域を 200 画素より速く横切らない。
    return ranged ? (std::min)(speed, span / 200.0) : speed;
}

// 有効 4 桁で末尾の 0 を省いた小数桁数（指数表記にしない）。
int DisplayDecimals(double value) {
    const double magnitude = std::abs(value);
    if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
        return 0;
    }
    int decimals = std::clamp(3 - static_cast<int>(std::floor(std::log10(magnitude))), 0, 6);
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    for (size_t length = std::strlen(text); decimals > 0 && text[length - 1u] == '0'; --length) {
        --decimals;
    }
    return decimals;
}

// ドラッグ中はその速さで動く桁まで（Alt の微調整なら 2 桁多く）出し、ImGui の丸めもその桁にする。
int DraggingDecimals(double speed) {
    const int decimals = static_cast<int>(std::ceil(-std::log10(speed))) + (ImGui::GetIO().KeyAlt ? 2 : 0);
    return std::clamp(decimals, 0, 6);
}

std::string FormatNumber(double value) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*f", DisplayDecimals(value), value);
    return text;
}

bool ReadClipboardNumber(double& value) {
    const char* text = ImGui::GetClipboardText();
    if (text == nullptr) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(text, &end);
    while (end != nullptr && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        ++end;
    }
    return end != text && end != nullptr && *end == '\0' && std::isfinite(value);
}

bool DrawNumberMenu(double& value, const NumberRange& range, const double* defaultValue, bool integer) {
    if (!ImGui::BeginPopupContextItem("##numberMenu")) {
        return false;
    }
    bool changed = false;
    if (defaultValue != nullptr) {
        const std::string label = TEXT::Localize("ui.number.reset") + " (" + FormatNumber(*defaultValue) + ")";
        if (ImGui::MenuItem(label.c_str(), nullptr, false, value != *defaultValue)) {
            value = *defaultValue;
            changed = true;
        }
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.number.copy").c_str())) {
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.9g", value);
        ImGui::SetClipboardText(text);
    }
    double pasted = 0.0;
    if (ImGui::MenuItem(TEXT::Localize("ui.number.paste").c_str(), nullptr, false, ReadClipboardNumber(pasted))) {
        value = integer ? std::round(pasted) : pasted;
        value = IsRanged(range) ? std::clamp(value, range.minimum, range.maximum) : value;
        changed = true;
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", TEXT::Localize("ui.number.hint").c_str());
    ImGui::EndPopup();
    return changed;
}

} // namespace

namespace DETAIL {

bool DragNumberComponent(const char* label, double& value, const NumberRange& range, const double* defaultValue,
                         bool integer) {
    const ImGuiID id = ImGui::GetID(label);
    const bool dragging = g_dragLatch.id == id;
    const double speed = dragging ? g_dragLatch.speed : DragSpeed(value, range, integer);
    const int decimals = integer ? 0 : (dragging ? DraggingDecimals(speed) : DisplayDecimals(value));
    char format[16]{};
    std::snprintf(format, sizeof(format), "%%.%df", decimals);
    const bool ranged = IsRanged(range);
    const double before = value;

    ImGui::BeginGroup();
    bool changed = ImGui::DragScalar(label, ImGuiDataType_Double, &value, static_cast<float>(speed),
                                     ranged ? &range.minimum : nullptr, ranged ? &range.maximum : nullptr, format,
                                     ranged ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
    if (ImGui::IsItemActivated()) {
        g_dragLatch = {id, DragSpeed(before, range, integer)};
    } else if (ImGui::IsItemDeactivated() && dragging) {
        g_dragLatch = {};
    }
    changed |= DrawNumberMenu(value, range, defaultValue, integer);
    ImGui::EndGroup();
    return changed;
}

} // namespace DETAIL

bool DragNumbers(const char* label, float* values, int count, const NumberRange& range, const float* defaults) {
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float total = ImGui::CalcItemWidth();
    const float each = (std::max)(1.0f, (total - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID(label);
    for (int index = 0; index < count; ++index) {
        if (index > 0) {
            ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PushID(index);
        ImGui::SetNextItemWidth(index + 1 == count ? (std::max)(1.0f, total - (each + spacing) * (count - 1)) : each);
        double value = values[index];
        const double fallback = defaults != nullptr ? defaults[index] : 0.0;
        if (DETAIL::DragNumberComponent("##value", value, range, defaults != nullptr ? &fallback : nullptr, false)) {
            values[index] = static_cast<float>(value);
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    // ImGui の DragFloat3 などと同じく、"##" より前の文字は欄の右へ出す。
    const char* hidden = std::strstr(label, "##");
    const size_t visible = hidden != nullptr ? static_cast<size_t>(hidden - label) : std::strlen(label);
    if (visible > 0u) {
        ImGui::SameLine(0.0f, spacing);
        ImGui::TextUnformatted(label, label + visible);
    }
    ImGui::EndGroup();
    return changed;
}

} // namespace HIKARI::EDITOR

#endif
