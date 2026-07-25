#include "Scene/Components/HIKARI_CharacterInputComponent.h"

namespace HIKARI {
    void CharacterInputComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["priority"] = priority_;
        out["moveAction"] = moveActionId_;
        out["lookAction"] = lookActionId_;
        out["jumpAction"] = jumpActionId_;
        out["sprintAction"] = sprintActionId_;
    }

    void CharacterInputComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        priority_ = in.value("priority", priority_);
        moveActionId_ = in.value("moveAction", moveActionId_);
        lookActionId_ = in.value("lookAction", lookActionId_);
        jumpActionId_ = in.value("jumpAction", jumpActionId_);
        sprintActionId_ = in.value("sprintAction", sprintActionId_);
    }

} // namespace HIKARI
