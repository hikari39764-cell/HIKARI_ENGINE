#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"

#include <algorithm>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI {

IrradianceVolumeComponentSettings NormalizeIrradianceVolumeSettings(IrradianceVolumeComponentSettings settings) {
    ClampLightProbeVolumeSettings(settings);
    if (settings.bakeSet.empty()) {
        settings.bakeSet = "Default";
    }
    if (settings.scenario.empty()) {
        settings.scenario = "Default";
    }
    return settings;
}

void IrradianceVolumeComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kIrradianceVolumeFields);
}

void IrradianceVolumeComponent::Deserialize(const nlohmann::json& in) {
    IrradianceVolumeComponentSettings decoded{};
    JSONFIELDS::Read(in, decoded, kIrradianceVolumeFields);
    settings_ = NormalizeIrradianceVolumeSettings(decoded);
}

bool IrradianceVolumeComponent::SetSize(const MATH::Vec3& size) noexcept {
    if (!MATH::IsFinite(size)) {
        return false;
    }
    const MATH::Vec3 clamped{(std::max)(size.x, 0.1f), (std::max)(size.y, 0.1f), (std::max)(size.z, 0.1f)};
    const MATH::Vec3& current = settings_.size;
    if (current.x == clamped.x && current.y == clamped.y && current.z == clamped.z) {
        return false;
    }
    settings_.size = clamped;
    settings_ = NormalizeIrradianceVolumeSettings(settings_);
    return true;
}

} // namespace HIKARI
