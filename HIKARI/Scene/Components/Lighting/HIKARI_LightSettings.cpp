#include "Scene/Components/Lighting/HIKARI_LightSettings.h"

#include <algorithm>

namespace HIKARI {

LightSettings NormalizeLightSettings(LightSettings settings) noexcept {
    settings = JSONFIELDS::Sanitize(settings, kLightSettingsFields);
    if (!TEXT::IsNamedEnum(RENDER3D::LIGHTING::kSceneLightTypeNames, settings.type)) {
        settings.type = RENDER3D::LIGHTING::SceneLightType::Point;
    }
    settings.outerConeAngleDegrees = (std::max)(settings.outerConeAngleDegrees, settings.innerConeAngleDegrees);
    return settings;
}

LightSettings ReadLightSettings(const nlohmann::json& properties) {
    LightSettings settings{};
    if (properties.is_object()) {
        JSONFIELDS::ReadLenient(properties, settings, kLightSettingsFields);
    }
    return NormalizeLightSettings(settings);
}

nlohmann::json WriteLightSettings(const LightSettings& settings) {
    nlohmann::json out = nlohmann::json::object();
    JSONFIELDS::Write(out, NormalizeLightSettings(settings), kLightSettingsFields);
    return out;
}

} // namespace HIKARI
