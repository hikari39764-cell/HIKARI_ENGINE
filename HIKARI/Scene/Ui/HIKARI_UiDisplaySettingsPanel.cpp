#include "Scene/Ui/HIKARI_UiDisplaySettings.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Presentation/HIKARI_DisplaySettingsService.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI/Project/FrameContext.h"
#include <algorithm>
#include <cmath>

namespace HIKARI {
namespace {
bool ChangesWindow(const GameProjectDisplaySettings& a, const GameProjectDisplaySettings& b) {
    return a.windowMode != b.windowMode || a.windowResolution != b.windowResolution;
}
bool BelongsTo(GameObject& object, const UiDisplaySettingsPanelComponent* panel) {
    for (auto* current = &object; current; current = current->GetParent())
        if (const auto* found = current->GetComponent<UiDisplaySettingsPanelComponent>())
            return found == panel;
    return false;
}
} // namespace
void UiDisplaySettingsPanelComponent::Serialize(nlohmann::json& out) const {
    out = {{"enabled", enabled_}, {"confirmSeconds", confirmSeconds_}};
}
void UiDisplaySettingsPanelComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = JSONREAD::BooleanMemberOr(in, "enabled", true);
    const float seconds = JSONREAD::NumberMemberOr(in, "confirmSeconds", 15.f);
    confirmSeconds_ = std::clamp(std::isfinite(seconds) ? seconds : 15.f, 5.f, 60.f);
    initialized_ = false;
    lastFrame_ = ~uint64_t(0);
}
void UiDisplaySettingsPanelComponent::BuildInspector(IInspectorBuilder& b) {
    if (!b.Section(TEXT::Localize("display.panel").c_str(), true))
        return;
    b.Bool(AuthoringFieldDescriptor{"enabled", TEXT::Localize("Enabled").c_str()}, enabled_);
    b.FloatRange(AuthoringFieldDescriptor{"confirmSeconds", TEXT::Localize("display.confirmSeconds").c_str()},
                 confirmSeconds_, 5, 60, 1);
    b.Text(TEXT::Localize("display.panelHelp"));
}
void UiDisplaySettingsPanelComponent::Tick(World& world, const FrameContext& frame) {
    const auto* play = world.Services().Find<RuntimePlayStateService>();
    if (!play || !play->IsActive()) {
        initialized_ = false;
        return;
    }
    if (lastFrame_ == frame.frameIndex)
        return;
    lastFrame_ = frame.frameIndex;
    auto* ui = world.Services().Find<GAME_UI::GameUiPresentationService>();
    GameProjectDisplaySettings actual;
    GameProjectDisplayCapabilities caps;
    if (!ui || !SERVICES::ReadDisplaySettings(actual) || !SERVICES::ReadDisplayCapabilities(caps))
        return;
    if (!initialized_) {
        draft_ = accepted_ = before_ = actual;
        phase_ = Phase::Editing;
        status_ = "display.ready";
        initialized_ = true;
    }
    const auto revert = [&] {
        if (SERVICES::ApplyDisplaySettings(world, before_) == GameProjectDisplayApplyResult::Accepted) {
            draft_ = before_;
            phase_ = Phase::Reverting;
            remaining_ = 5.f;
            status_ = "display.reverting";
        } else {
            phase_ = Phase::Editing;
            draft_ = accepted_ = actual;
            status_ = "display.failed";
        }
    };
    if (phase_ != Phase::Editing) {
        if (std::isfinite(frame.unscaledDt) && frame.unscaledDt > 0)
            remaining_ -= frame.unscaledDt;
        if (phase_ == Phase::Applying && actual == draft_) {
            if (ChangesWindow(before_, draft_)) {
                phase_ = Phase::Confirming;
                remaining_ = confirmSeconds_;
                status_ = "display.confirm";
            } else {
                accepted_ = actual;
                phase_ = Phase::Editing;
                status_ = "display.applied";
            }
        } else if (phase_ == Phase::Reverting && actual == before_) {
            accepted_ = draft_ = actual;
            phase_ = Phase::Editing;
            status_ = "display.reverted";
        } else if (remaining_ <= 0) {
            if (phase_ == Phase::Reverting) {
                phase_ = Phase::Editing;
                accepted_ = draft_ = actual;
                status_ = "display.failed";
            } else
                revert();
        }
    } else if (draft_ == accepted_ && actual != accepted_) {
        draft_ = accepted_ = actual;
    }
    world.ForEachObjectWith<UiDisplaySettingComponent>([&](GameObject& object, UiDisplaySettingComponent& control) {
        if (!BelongsTo(object, this))
            return;
        auto* button = object.GetComponent<UiButtonComponent>();
        auto* text = object.GetComponent<UiTextComponent>();
        const auto& action = control.Action();
        const bool activated =
            enabled_ && caps.gameWindowActive && ui->WasButtonActivated(object.GetRuntimeHandle(), frame.frameIndex);
        bool interactable = enabled_ && caps.gameWindowActive;
        std::string label;
        if (action == "Cycle") {
            const auto fields = UiDisplaySettings();
            const auto found =
                std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return control.Setting() == f.id; });
            if (found == fields.end()) {
                interactable = false;
                label = TEXT::Localize("display.invalid");
            } else {
                auto candidate = draft_;
                interactable = interactable && phase_ == Phase::Editing &&
                               CycleUiDisplaySetting(candidate, *found, caps, control.Direction());
                if (activated && interactable) {
                    draft_ = candidate;
                    status_ = "display.changed";
                }
                const int index = found->read(draft_);
                label = index >= found->minimum && index < static_cast<int>(found->choices.size())
                            ? TEXT::Localize(found->choices[index])
                            : TEXT::Localize("display.unsupported");
            }
        } else if (action == "Apply") {
            interactable = interactable && phase_ == Phase::Editing && draft_ != actual;
            if (activated && interactable) {
                before_ = actual;
                if (SERVICES::ApplyDisplaySettings(world, draft_) == GameProjectDisplayApplyResult::Accepted) {
                    phase_ = Phase::Applying;
                    remaining_ = 5.f;
                    status_ = "display.applying";
                } else
                    status_ = "display.failed";
            }
            label = TEXT::Localize("display.apply");
        } else if (action == "Keep") {
            interactable = interactable && phase_ == Phase::Confirming;
            if (activated && interactable) {
                accepted_ = actual;
                phase_ = Phase::Editing;
                status_ = "display.applied";
            }
            label = TEXT::Localize("display.keep");
        } else if (action == "Revert") {
            interactable =
                interactable && (phase_ == Phase::Confirming || (phase_ == Phase::Editing && draft_ != actual));
            if (activated && interactable) {
                if (phase_ == Phase::Confirming)
                    revert();
                else {
                    draft_ = accepted_ = actual;
                    status_ = "display.ready";
                }
            }
            label = TEXT::Localize("display.revert");
        } else {
            interactable = false;
            label = TEXT::Localize(caps.gameWindowActive ? status_ : "display.gameWindowRequired");
            if (phase_ == Phase::Confirming)
                label += " (" + std::to_string(static_cast<int>(std::ceil(remaining_))) + "s)";
        }
        if (button)
            button->SetInteractable(interactable);
        if (text)
            text->SetText(label);
    });
}
void UpdateUiDisplaySettings(World& world, const FrameContext& frame) {
    world.ForEachObjectWith<UiDisplaySettingsPanelComponent>(
        [&](GameObject&, UiDisplaySettingsPanelComponent& panel) { panel.Tick(world, frame); });
}
} // namespace HIKARI
