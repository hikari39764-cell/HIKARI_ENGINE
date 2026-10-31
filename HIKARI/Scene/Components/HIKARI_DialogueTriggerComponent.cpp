#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"

#include <utility>

namespace HIKARI {

void DialogueTriggerComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kDialogueTriggerFields);
}

void DialogueTriggerComponent::Deserialize(const nlohmann::json& in) {
    DialogueTriggerSettings decoded{};
    JSONFIELDS::Read(in, decoded, kDialogueTriggerFields);
    settings_ = std::move(decoded);
    consumed_ = false;
}

} // namespace HIKARI
