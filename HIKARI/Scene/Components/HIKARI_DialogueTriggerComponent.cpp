#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"

namespace HIKARI {

void DialogueTriggerComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["dialogueAssetGuid"] = dialogueAssetGuid_.value;
    out["locale"] = locale_;
    out["activation"] = TEXT::EnumToName(kDialogueTriggerActivationNames, activation_, "OnEnter");
    out["interactAction"] = interactActionId_;
    out["pauseWorld"] = pauseWorld_;
    out["oneShot"] = oneShot_;
}

void DialogueTriggerComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    dialogueAssetGuid_.value = in.value("dialogueAssetGuid", dialogueAssetGuid_.value);
    locale_ = in.value("locale", locale_);
    interactActionId_ = in.value("interactAction", interactActionId_);
    pauseWorld_ = in.value("pauseWorld", pauseWorld_);
    oneShot_ = in.value("oneShot", oneShot_);
    TEXT::TryParseEnum(kDialogueTriggerActivationNames, in.value("activation", std::string{}), activation_);
    consumed_ = false;
}

} // namespace HIKARI
