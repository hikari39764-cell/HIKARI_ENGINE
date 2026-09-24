#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

namespace {
WATER::WaterSurfacePreset ReadPreset(const nlohmann::json& source, WATER::WaterSurfacePreset fallback) {
    if (const nlohmann::json* preset = JSONREAD::FindMember(source, "preset")) {
        if (preset->is_string()) {
            return WATER::ParseWaterSurfacePreset(preset->get<std::string>(), fallback);
        }
        if (preset->is_number_integer() || preset->is_number_unsigned()) {
            const int value = JSONREAD::IntegerMemberOr(source, "preset", -1);
            return WATER::ParseWaterSurfacePreset(value, fallback);
        }
    }

    // 原型の style は Realistic=0 / Stylized=1 だった。
    if (const nlohmann::json* style = JSONREAD::FindMember(source, "style");
        style != nullptr && (style->is_number_integer() || style->is_number_unsigned())) {
        return JSONREAD::IntegerMemberOr(source, "style", 1) == 0 ? WATER::WaterSurfacePreset::RealisticClearSummer
                                                                  : WATER::WaterSurfacePreset::AnimeSummer;
    }
    return fallback;
}
} // namespace

const WATER::WaterSurfaceSettings& WaterSurfaceComponent::GetSettings() const noexcept { return settings_; }

void WaterSurfaceComponent::SetSettings(const WATER::WaterSurfaceSettings& settings) {
    const WATER::WaterSurfaceSettings sanitized = WATER::SanitizeWaterSurfaceSettings(settings);
    if (WATER::WaterSurfaceSettingsEqual(settings_, sanitized)) {
        return;
    }
    settings_ = sanitized;
    NotifyRenderStateDirty();
}

void WaterSurfaceComponent::ApplyPreset(WATER::WaterSurfacePreset preset) {
    const WATER::WaterSurfaceGeometryMode geometryMode = settings_.geometryMode;
    WATER::WaterSurfaceSettings next = WATER::MakeWaterSurfacePresetSettings(preset);
    next.geometryMode = geometryMode;
    SetSettings(next);
}

void WaterSurfaceComponent::NotifyRenderStateDirty() {
    if (GameObject* owner = GetOwner()) {
        owner->MarkRenderStateDirty();
    }
}

void WaterSurfaceComponent::Serialize(nlohmann::json& out) const {
    const WATER::WaterSurfaceSettings settings = WATER::SanitizeWaterSurfaceSettings(settings_);
    out = {{"_schemaVersion", WATER::kWaterSurfaceSchemaVersion},
           {"preset", WATER::ToString(settings.preset)},
           {"geometryMode", WATER::ToString(settings.geometryMode)},
           {"timeScale", settings.timeScale},
           {"choppiness", settings.choppiness},
           {"microWaveStrength", settings.microWaveStrength},
           {"depthTransmittance",
            {settings.depthTransmittance.x, settings.depthTransmittance.y, settings.depthTransmittance.z}},
           {"scatteringColor", {settings.scatteringColor.x, settings.scatteringColor.y, settings.scatteringColor.z}},
           {"shallowClearDepth", settings.shallowClearDepth},
           {"shallowBlendDepth", settings.shallowBlendDepth},
           {"shallowGraphicWeight", settings.shallowGraphicWeight},
           {"foamColor", {settings.foamColor.x, settings.foamColor.y, settings.foamColor.z}},
           {"foamAmount", settings.foamAmount},
           {"absorptionDistance", settings.absorptionDistance},
           {"surfaceRoughness", settings.surfaceRoughness},
           {"directSunStrength", settings.directSunStrength},
           {"transmissionStrength", settings.transmissionStrength},
           {"shadowStrength", settings.shadowStrength},
           {"reflectionStrength", settings.reflectionStrength},
           {"sunGlintStrength", settings.sunGlintStrength},
           {"glintDensity", settings.glintDensity},
           {"glintFlowSpeed", settings.glintFlowSpeed},
           {"ribbonWidth", settings.ribbonWidth},
           {"windDirectionDeg", settings.windDirectionDeg},
           {"windSpeed", settings.windSpeed},
           {"shortWaveStrength", settings.shortWaveStrength},
           {"capillaryStrength", settings.capillaryStrength},
           {"waves", nlohmann::json::array()}};
    for (const WATER::WaterGerstnerWave& wave : settings.waves) {
        out["waves"].push_back(nlohmann::json{{"directionDeg", wave.directionDeg},
                                              {"amplitude", wave.amplitude},
                                              {"wavelength", wave.wavelength},
                                              {"steepness", wave.steepness}});
    }
}

void WaterSurfaceComponent::Deserialize(const nlohmann::json& in) {
    if (!in.is_object()) {
        return;
    }

    const WATER::WaterSurfacePreset preset = ReadPreset(in, settings_.preset);
    WATER::WaterSurfaceSettings next = WATER::MakeWaterSurfacePresetSettings(preset);
    next.preset = preset;
    if (const nlohmann::json* geometryMode = JSONREAD::FindMember(in, "geometryMode");
        geometryMode != nullptr && geometryMode->is_string()) {
        next.geometryMode = WATER::ParseWaterSurfaceGeometryMode(geometryMode->get<std::string>(),
                                                                 WATER::WaterSurfaceGeometryMode::FiniteAuthored);
    }
    next.timeScale = JSONREAD::NumberMemberOr(in, "timeScale", next.timeScale);
    next.choppiness = JSONREAD::NumberMemberOr(in, "choppiness", next.choppiness);
    next.microWaveStrength = JSONREAD::NumberMemberOr(
        in, "microWaveStrength", JSONREAD::NumberMemberOr(in, "detailStrength", next.microWaveStrength));

    next.depthTransmittance = JSONREAD::Vec3MemberOr(in, "depthTransmittance",
                                                     JSONREAD::Vec3MemberOr(in, "deepColor", next.depthTransmittance));
    next.scatteringColor =
        JSONREAD::Vec3MemberOr(in, "scatteringColor", JSONREAD::Vec3MemberOr(in, "shallowColor", next.scatteringColor));
    next.shallowClearDepth = JSONREAD::NumberMemberOr(in, "shallowClearDepth", next.shallowClearDepth);
    next.shallowBlendDepth = JSONREAD::NumberMemberOr(in, "shallowBlendDepth", next.shallowBlendDepth);
    next.shallowGraphicWeight = JSONREAD::NumberMemberOr(in, "shallowGraphicWeight", next.shallowGraphicWeight);
    next.foamColor = JSONREAD::Vec3MemberOr(in, "foamColor", next.foamColor);
    next.foamAmount = JSONREAD::NumberMemberOr(in, "foamAmount", next.foamAmount);
    next.surfaceRoughness = JSONREAD::NumberMemberOr(in, "surfaceRoughness", next.surfaceRoughness);
    next.directSunStrength = JSONREAD::NumberMemberOr(in, "directSunStrength", next.directSunStrength);
    next.transmissionStrength = JSONREAD::NumberMemberOr(in, "transmissionStrength", next.transmissionStrength);
    next.shadowStrength = JSONREAD::NumberMemberOr(in, "shadowStrength", next.shadowStrength);
    next.reflectionStrength = JSONREAD::NumberMemberOr(in, "reflectionStrength", next.reflectionStrength);
    next.sunGlintStrength = JSONREAD::NumberMemberOr(in, "sunGlintStrength", next.sunGlintStrength);
    next.glintDensity = JSONREAD::NumberMemberOr(in, "glintDensity", next.glintDensity);
    next.glintFlowSpeed = JSONREAD::NumberMemberOr(in, "glintFlowSpeed", next.glintFlowSpeed);
    next.ribbonWidth = JSONREAD::NumberMemberOr(in, "ribbonWidth", next.ribbonWidth);
    next.windDirectionDeg = JSONREAD::NumberMemberOr(in, "windDirectionDeg", next.windDirectionDeg);
    next.windSpeed = JSONREAD::NumberMemberOr(in, "windSpeed", next.windSpeed);
    next.shortWaveStrength = JSONREAD::NumberMemberOr(in, "shortWaveStrength", next.shortWaveStrength);
    next.capillaryStrength = JSONREAD::NumberMemberOr(in, "capillaryStrength", next.capillaryStrength);

    if (JSONREAD::FindMember(in, "absorptionDistance") != nullptr) {
        next.absorptionDistance = JSONREAD::NumberMemberOr(in, "absorptionDistance", next.absorptionDistance);
    } else if (const nlohmann::json* legacyAbsorption = JSONREAD::FindMember(in, "absorption");
               legacyAbsorption != nullptr && legacyAbsorption->is_number()) {
        const float coefficient = legacyAbsorption->get<float>();
        if (std::isfinite(coefficient) && coefficient > 1.0e-4f) {
            next.absorptionDistance = 1.0f / coefficient;
        }
    }

    if (const nlohmann::json* waves = JSONREAD::FindMember(in, "waves"); waves != nullptr && waves->is_array()) {
        const std::size_t count = (std::min)(waves->size(), next.waves.size());
        for (std::size_t index = 0; index < count; ++index) {
            const nlohmann::json& source = (*waves)[index];
            if (!source.is_object()) {
                continue;
            }
            WATER::WaterGerstnerWave& wave = next.waves[index];
            wave.directionDeg = JSONREAD::NumberMemberOr(source, "directionDeg", wave.directionDeg);
            wave.amplitude = JSONREAD::NumberMemberOr(source, "amplitude", wave.amplitude);
            wave.wavelength = JSONREAD::NumberMemberOr(source, "wavelength", wave.wavelength);
            wave.steepness = JSONREAD::NumberMemberOr(source, "steepness", wave.steepness);
        }
    }

    SetSettings(next);
}

} // namespace HIKARI
