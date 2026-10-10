#include "Scene/Components/Lighting/HIKARI_LightSettings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

namespace HIKARI {

namespace {
std::atomic<float> gSceneKeyIlluminance{kDefaultSceneKeyIlluminance};
} // namespace

LightSettings NormalizeLightSettings(LightSettings settings) noexcept {
    settings = JSONFIELDS::Sanitize(settings, kLightSettingsFields);
    if (!TEXT::IsNamedEnum(RENDER3D::LIGHTING::kSceneLightTypeNames, settings.type)) {
        settings.type = RENDER3D::LIGHTING::SceneLightType::Point;
    }
    if (!TEXT::IsNamedEnum(kLightIntensityUnitNames, settings.intensityUnit)) {
        settings.intensityUnit = LightIntensityUnit::Lumens;
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

void SetSceneKeyIlluminance(float lux) noexcept {
    if (std::isfinite(lux) && lux > 0.0f) {
        gSceneKeyIlluminance.store(lux, std::memory_order_relaxed);
    }
}

float GetSceneKeyIlluminance() noexcept { return gSceneKeyIlluminance.load(std::memory_order_relaxed); }

float ResolveLuminousIntensity(const LightSettings& settings) noexcept {
    if (settings.intensityUnit == LightIntensityUnit::SceneRelative) {
        return std::isfinite(settings.sceneRelativeIntensity)
                   ? (std::max)(settings.sceneRelativeIntensity, 0.0f) * GetSceneKeyIlluminance()
                   : 0.0f;
    }
    return RENDER3D::LIGHTING::LuminousIntensityFromFlux(settings.intensity);
}

float GetLightBrightness(const LightSettings& settings) noexcept {
    return settings.intensityUnit == LightIntensityUnit::SceneRelative ? settings.sceneRelativeIntensity
                                                                       : settings.intensity;
}

void SetLightBrightness(LightSettings& settings, float brightness) noexcept {
    (settings.intensityUnit == LightIntensityUnit::SceneRelative ? settings.sceneRelativeIntensity
                                                                 : settings.intensity) = brightness;
}

LightSettings ConvertLightIntensityUnit(LightSettings settings, LightIntensityUnit unit) noexcept {
    if (settings.intensityUnit == unit) {
        return settings;
    }
    const float candela = ResolveLuminousIntensity(settings);
    settings.intensityUnit = unit;
    if (unit == LightIntensityUnit::SceneRelative) {
        settings.sceneRelativeIntensity =
            std::clamp(candela / GetSceneKeyIlluminance(), 0.0f, kMaximumSceneRelativeIntensity);
    } else {
        settings.intensity = std::clamp(candela * 4.0f * std::numbers::pi_v<float>, 0.0f, kMaximumLightLumens);
    }
    return settings;
}

} // namespace HIKARI
