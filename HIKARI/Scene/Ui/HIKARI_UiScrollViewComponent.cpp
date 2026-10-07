#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI {
namespace {

constexpr TEXT::EnumName<UiScrollAxis> kUiScrollAxisNames[] = {
    {UiScrollAxis::Vertical, "Vertical"},
    {UiScrollAxis::Horizontal, "Horizontal"},
};

const char* L(const char* key) { return TEXT::Localize(key).c_str(); }

constexpr auto kUiScrollViewSettingsFields = [](auto&& field) {
    field("enabled", &UiScrollViewSettings::enabled);
    field("axis", &UiScrollViewSettings::axis, kUiScrollAxisNames);
    field("contentExtent", &UiScrollViewSettings::contentExtent, JSONFIELDS::Range{0.0, 65536.0});
    field("scrollSpeed", &UiScrollViewSettings::scrollSpeed, JSONFIELDS::Range{1.0, 2048.0});
};

} // namespace

void UiScrollViewComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1}};
    JSONFIELDS::Write(out, settings_, kUiScrollViewSettingsFields);
}

void UiScrollViewComponent::Deserialize(const nlohmann::json& in) {
    settings_ = {};
    JSONFIELDS::ReadLenient(in, settings_, kUiScrollViewSettingsFields);
    settings_ = JSONFIELDS::Sanitize(std::move(settings_), kUiScrollViewSettingsFields);
    if (!TEXT::IsNamedEnum(kUiScrollAxisNames, settings_.axis))
        settings_.axis = UiScrollAxis::Vertical;
    resolvedContentExtent_ = 0.0f;
    scrollOffset_ = 0.0f;
}

void UiScrollViewComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("uix.scrollView"), true))
        return;
    (void)builder.Bool({"enabled", L("uix.enabled"), L("uix.scrollEnabledHelp")}, settings_.enabled);
    constexpr auto axisValues = TEXT::EnumNameList(kUiScrollAxisNames);
    const std::array<AuthoringChoice, 2> axes{
        {{axisValues[0], L("uix.vertical")}, {axisValues[1], L("uix.horizontal")}}};
    int axis = static_cast<int>(settings_.axis);
    if (builder.Choice({"axis", L("uix.scrollAxis"), L("uix.scrollAxisHelp")}, axis, axes))
        settings_.axis = TEXT::IsNamedEnum(kUiScrollAxisNames, static_cast<UiScrollAxis>(axis))
                             ? static_cast<UiScrollAxis>(axis)
                             : UiScrollAxis::Vertical;
    (void)builder.FloatRange({"contentExtent", L("uix.contentExtent"), L("uix.contentExtentHelp")},
                             settings_.contentExtent, 0.0f, 65536.0f, 1.0f);
    (void)builder.FloatRange({"scrollSpeed", L("uix.scrollSpeed"), L("uix.scrollSpeedHelp")}, settings_.scrollSpeed,
                             1.0f, 2048.0f, 1.0f);
    settings_ = JSONFIELDS::Sanitize(std::move(settings_), kUiScrollViewSettingsFields);
}

float UiScrollViewComponent::GetScrollOffset(float viewportExtent) const noexcept {
    const float viewport = std::isfinite(viewportExtent) ? (std::max)(viewportExtent, 0.0f) : 0.0f;
    const float maximum = (std::max)(GetEffectiveContentExtent() - viewport, 0.0f);
    return std::clamp(scrollOffset_, 0.0f, maximum);
}

void UiScrollViewComponent::SetScrollOffset(float value, float viewportExtent) noexcept {
    if (std::isfinite(value)) {
        const float viewport = std::isfinite(viewportExtent) ? (std::max)(viewportExtent, 0.0f) : 0.0f;
        scrollOffset_ = std::clamp(value, 0.0f, (std::max)(GetEffectiveContentExtent() - viewport, 0.0f));
    }
}

void UiScrollViewComponent::SetResolvedContentExtent(float value) noexcept {
    resolvedContentExtent_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 65536.0f);
}

bool UiScrollViewComponent::ScrollBy(float delta, float viewportExtent) noexcept {
    if (!std::isfinite(delta))
        return false;
    const float previous = GetScrollOffset(viewportExtent);
    SetScrollOffset(previous + delta, viewportExtent);
    return GetScrollOffset(viewportExtent) != previous;
}

} // namespace HIKARI
