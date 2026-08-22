#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"

namespace HIKARI {

    void DialogueInteractorComponent::Serialize(
        nlohmann::json& out) const {
        out["enabled"] = enabled_;
    }

    void DialogueInteractorComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
    }

} // namespace HIKARI
