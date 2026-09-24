#pragma once
#include "Scene/Components/HIKARI_IComponent.h"
#include "HIKARI/Project/DisplaySettingsTypes.h"
#include <span>

namespace HIKARI {
struct FrameContext;
struct UiDisplaySettingDescriptor {
    const char* id;
    const char* label;
    std::span<const char* const> choices;
    int minimum;
    int (*read)(const GameProjectDisplaySettings&);
    void (*write)(GameProjectDisplaySettings&, int);
};
std::span<const UiDisplaySettingDescriptor> UiDisplaySettings();
bool CycleUiDisplaySetting(GameProjectDisplaySettings&, const UiDisplaySettingDescriptor&,
                           const GameProjectDisplayCapabilities&, int direction);

// パネルは一つの編集ドラフトを所有する。UI は実行中の値だけを変更し、Scene を書き換えない。
class UiDisplaySettingsPanelComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiDisplaySettingsPanelComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    void Tick(World&, const FrameContext&);

private:
    bool enabled_ = true, initialized_ = false;
    float confirmSeconds_ = 15.f, remaining_ = 0.f;
    enum class Phase { Editing, Applying, Confirming, Reverting };
    Phase phase_ = Phase::Editing;
    GameProjectDisplaySettings draft_{}, accepted_{}, before_{};
    uint64_t lastFrame_ = ~uint64_t(0);
    std::string status_;
};

class UiDisplaySettingComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiDisplaySettingComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    const std::string& Action() const { return action_; }
    const std::string& Setting() const { return setting_; }
    int Direction() const { return direction_; }

private:
    std::string action_ = "Cycle", setting_ = "AntiAliasing";
    int direction_ = 1;
};
void UpdateUiDisplaySettings(World&, const FrameContext&);
} // namespace HIKARI
