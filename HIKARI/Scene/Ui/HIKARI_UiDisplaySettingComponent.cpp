#include "Scene/Ui/HIKARI_UiDisplaySettings.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <array>
#include <type_traits>

namespace HIKARI {
namespace {
constexpr const char* aa[]{"Off", "FXAA", "TAA", "DLAA", "DLSS"};
constexpr const char* resolution[]{"display.viewport", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160"};
constexpr const char* window[]{"display.windowed", "display.borderless", "display.fullscreen"};
constexpr const char* quality[]{"display.quality", "display.balanced", "display.performance",
                                "display.ultraPerformance"};
constexpr const char* toggle[]{"Off", "On"};
template <auto Member>
constexpr UiDisplaySettingDescriptor Field(const char* id, const char* label, std::span<const char* const> choices,
                                           int minimum = 0) {
    return {id,
            label,
            choices,
            minimum,
            [](const GameProjectDisplaySettings& s) { return static_cast<int>(s.*Member); },
            [](GameProjectDisplaySettings& s, int n) {
                s.*Member = static_cast<std::remove_cvref_t<decltype(s.*Member)>>(n);
            }};
}
constexpr UiDisplaySettingDescriptor fields[]{
    Field<&GameProjectDisplaySettings::antiAliasing>("AntiAliasing", "display.aa", aa),
    Field<&GameProjectDisplaySettings::renderResolution>("RenderResolution", "display.renderResolution", resolution),
    Field<&GameProjectDisplaySettings::windowResolution>("WindowResolution", "display.windowResolution", resolution, 1),
    Field<&GameProjectDisplaySettings::windowMode>("WindowMode", "display.windowMode", window),
    Field<&GameProjectDisplaySettings::dlssQuality>("DlssQuality", "display.dlssQuality", quality),
    Field<&GameProjectDisplaySettings::frameGeneration>("FrameGeneration", "display.frameGeneration", toggle),
    Field<&GameProjectDisplaySettings::vSync>("VSync", "display.vsync", toggle)};
} // namespace
std::span<const UiDisplaySettingDescriptor> UiDisplaySettings() { return fields; }
bool CycleUiDisplaySetting(GameProjectDisplaySettings& value, const UiDisplaySettingDescriptor& field,
                           const GameProjectDisplayCapabilities& caps, int direction) {
    const std::string_view id = field.id;
    if (!caps.gameWindowActive || (id == "FrameGeneration" && !caps.frameGenerationSupported) ||
        (id == "DlssQuality" && value.antiAliasing != GameProjectAntiAliasing::DLSS))
        return false;
    int next = field.read(value);
    const int count = static_cast<int>(field.choices.size());
    for (int i = 0; i < count; ++i) {
        next += direction < 0 ? -1 : 1;
        if (next < field.minimum)
            next = count - 1;
        if (next >= count)
            next = field.minimum;
        if (id == "AntiAliasing" && !caps.Supports(static_cast<GameProjectAntiAliasing>(next)))
            continue;
        if (next == field.read(value))
            return false;
        field.write(value, next);
        return true;
    }
    return false;
}
void UiDisplaySettingComponent::Serialize(nlohmann::json& out) const {
    out = {{"action", action_}, {"setting", setting_}, {"direction", direction_}};
}
void UiDisplaySettingComponent::Deserialize(const nlohmann::json& in) {
    action_ = JSONREAD::StringMemberOr(in, "action", "Cycle");
    setting_ = JSONREAD::StringMemberOr(in, "setting", "AntiAliasing");
    direction_ = JSONREAD::IntegerMemberOr(in, "direction", 1) < 0 ? -1 : 1;
}
void UiDisplaySettingComponent::BuildInspector(IInspectorBuilder& b) {
    const auto L = [](const char* key) { return TEXT::LocalizeCStr(key); };
    if (!b.Section(L("display.control"), true))
        return;
    const std::array<AuthoringChoice, 5> actions{{{"Cycle", L("display.cycle")},
                                                  {"Apply", L("display.apply")},
                                                  {"Revert", L("display.revert")},
                                                  {"Keep", L("display.keep")},
                                                  {"Status", L("display.status")}}};
    int index = 0;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i)
        if (actions[i].id == action_)
            index = i;
    if (b.Choice(AuthoringFieldDescriptor{"action", L("display.action")}, index, actions))
        action_ = actions[index].id;
    if (action_ != "Cycle")
        return;
    std::vector<AuthoringChoice> choices;
    index = 0;
    for (const auto& f : fields) {
        if (setting_ == f.id)
            index = static_cast<int>(choices.size());
        choices.push_back({f.id, L(f.label)});
    }
    if (b.Choice(AuthoringFieldDescriptor{"setting", L("display.setting")}, index, choices))
        setting_ = choices[index].id;
    const std::array<AuthoringChoice, 2> directions{{{"-1", L("display.previous")}, {"1", L("display.next")}}};
    index = direction_ < 0 ? 0 : 1;
    if (b.Choice(AuthoringFieldDescriptor{"direction", L("display.direction")}, index, directions))
        direction_ = index == 0 ? -1 : 1;
}
} // namespace HIKARI
