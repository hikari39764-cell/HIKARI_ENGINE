#include "Scene/Components/Lighting/HIKARI_LightComponent.h"

namespace HIKARI {

void LightComponent::Serialize(nlohmann::json& out) const { out = WriteLightSettings(settings_); }

void LightComponent::Deserialize(const nlohmann::json& in) { SetSettings(ReadLightSettings(in)); }

void LightComponent::SetSettings(LightSettings next) noexcept {
    next = NormalizeLightSettings(next);
    if (next == settings_) {
        return;
    }
    settings_ = next;
    ++stateRevision_;
    if (stateRevision_ == 0u) {
        stateRevision_ = 1u;
    }
}

} // namespace HIKARI
