#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"

namespace HIKARI {

    const char* ToString(DialogueTriggerActivation value) noexcept {
        switch (value) {
        case DialogueTriggerActivation::OnEnter: return "OnEnter";
        case DialogueTriggerActivation::Interact: return "Interact";
        default: return "OnEnter";
        }
    }

    bool TryParseDialogueTriggerActivation(
        std::string_view text,
        DialogueTriggerActivation& outValue) noexcept {
        if (text == "OnEnter") {
            outValue = DialogueTriggerActivation::OnEnter;
            return true;
        }
        if (text == "Interact") {
            outValue = DialogueTriggerActivation::Interact;
            return true;
        }
        return false;
    }

    void DialogueTriggerComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["dialogueAssetGuid"] = dialogueAssetGuid_.value;
        out["locale"] = locale_;
        out["activation"] = ToString(activation_);
        out["interactAction"] = interactActionId_;
        out["pauseWorld"] = pauseWorld_;
        out["oneShot"] = oneShot_;
    }

    void DialogueTriggerComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        dialogueAssetGuid_.value = in.value(
            "dialogueAssetGuid", dialogueAssetGuid_.value);
        locale_ = in.value("locale", locale_);
        interactActionId_ = in.value(
            "interactAction", interactActionId_);
        pauseWorld_ = in.value("pauseWorld", pauseWorld_);
        oneShot_ = in.value("oneShot", oneShot_);
        DialogueTriggerActivation parsed = activation_;
        if (TryParseDialogueTriggerActivation(
                in.value("activation", std::string(ToString(activation_))),
                parsed)) {
            activation_ = parsed;
        }
        consumed_ = false;
    }

} // namespace HIKARI
