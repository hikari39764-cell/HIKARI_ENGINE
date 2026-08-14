#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    namespace {
        WATER::WaterSurfacePreset ReadPreset(
            const nlohmann::json& source,
            WATER::WaterSurfacePreset fallback) {

            if (const nlohmann::json* preset =
                    JSONREAD::FindMember(source, "preset")) {
                if (preset->is_string()) {
                    return WATER::ParseWaterSurfacePreset(
                        preset->get<std::string>(),
                        fallback);
                }
                if (preset->is_number_integer() ||
                    preset->is_number_unsigned()) {
                    const int value = JSONREAD::IntegerMemberOr(
                        source,
                        "preset",
                        -1);
                    if (value == static_cast<int>(
                            WATER::WaterSurfacePreset::AnimeSummer)) {
                        return WATER::WaterSurfacePreset::AnimeSummer;
                    }
                    if (value == static_cast<int>(
                            WATER::WaterSurfacePreset::
                                RealisticClearSummer)) {
                        return WATER::WaterSurfacePreset::
                            RealisticClearSummer;
                    }
                }
            }

            // 原型の style は Realistic=0 / Stylized=1 だった。
            if (const nlohmann::json* style =
                    JSONREAD::FindMember(source, "style");
                style != nullptr &&
                (style->is_number_integer() ||
                    style->is_number_unsigned())) {
                return JSONREAD::IntegerMemberOr(
                        source,
                        "style",
                        1) == 0
                    ? WATER::WaterSurfacePreset::RealisticClearSummer
                    : WATER::WaterSurfacePreset::AnimeSummer;
            }
            return fallback;
        }
    }

    const WATER::WaterSurfaceSettings&
        WaterSurfaceComponent::GetSettings() const noexcept {
        return settings_;
    }

    void WaterSurfaceComponent::SetSettings(
        const WATER::WaterSurfaceSettings& settings) {

        const WATER::WaterSurfaceSettings sanitized =
            WATER::SanitizeWaterSurfaceSettings(settings);
        if (WATER::WaterSurfaceSettingsEqual(settings_, sanitized)) {
            return;
        }
        settings_ = sanitized;
        NotifyRenderStateDirty();
    }

    void WaterSurfaceComponent::ApplyPreset(
        WATER::WaterSurfacePreset preset) {

        SetSettings(WATER::MakeWaterSurfacePresetSettings(preset));
    }

    void WaterSurfaceComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void WaterSurfaceComponent::Serialize(nlohmann::json& out) const {
        const WATER::WaterSurfaceSettings settings =
            WATER::SanitizeWaterSurfaceSettings(settings_);
        out = {
            { "_schemaVersion", WATER::kWaterSurfaceSchemaVersion },
            { "preset", WATER::ToString(settings.preset) },
            { "timeScale", settings.timeScale },
            { "choppiness", settings.choppiness },
            { "microWaveStrength", settings.microWaveStrength },
            { "depthTransmittance", {
                settings.depthTransmittance.x,
                settings.depthTransmittance.y,
                settings.depthTransmittance.z } },
            { "scatteringColor", {
                settings.scatteringColor.x,
                settings.scatteringColor.y,
                settings.scatteringColor.z } },
            { "foamColor", {
                settings.foamColor.x,
                settings.foamColor.y,
                settings.foamColor.z } },
            { "foamAmount", settings.foamAmount },
            { "absorptionDistance", settings.absorptionDistance },
            { "surfaceRoughness", settings.surfaceRoughness },
            { "waves", nlohmann::json::array() }
        };
        for (const WATER::WaterGerstnerWave& wave : settings.waves) {
            out["waves"].push_back(nlohmann::json{
                { "directionDeg", wave.directionDeg },
                { "amplitude", wave.amplitude },
                { "wavelength", wave.wavelength },
                { "steepness", wave.steepness }
            });
        }
    }

    void WaterSurfaceComponent::Deserialize(const nlohmann::json& in) {
        if (!in.is_object()) {
            return;
        }

        const WATER::WaterSurfacePreset preset = ReadPreset(
            in,
            settings_.preset);
        WATER::WaterSurfaceSettings next =
            WATER::MakeWaterSurfacePresetSettings(preset);
        next.preset = preset;
        next.timeScale = JSONREAD::NumberMemberOr(
            in,
            "timeScale",
            next.timeScale);
        next.choppiness = JSONREAD::NumberMemberOr(
            in,
            "choppiness",
            next.choppiness);
        next.microWaveStrength = JSONREAD::NumberMemberOr(
            in,
            "microWaveStrength",
            JSONREAD::NumberMemberOr(
                in,
                "detailStrength",
                next.microWaveStrength));

        next.depthTransmittance = JSONREAD::Vec3MemberOr(
            in,
            "depthTransmittance",
            JSONREAD::Vec3MemberOr(
                in,
                "deepColor",
                next.depthTransmittance));
        next.scatteringColor = JSONREAD::Vec3MemberOr(
            in,
            "scatteringColor",
            JSONREAD::Vec3MemberOr(
                in,
                "shallowColor",
                next.scatteringColor));
        next.foamColor = JSONREAD::Vec3MemberOr(
            in,
            "foamColor",
            next.foamColor);
        next.foamAmount = JSONREAD::NumberMemberOr(
            in,
            "foamAmount",
            next.foamAmount);
        next.surfaceRoughness = JSONREAD::NumberMemberOr(
            in,
            "surfaceRoughness",
            next.surfaceRoughness);

        if (JSONREAD::FindMember(in, "absorptionDistance") != nullptr) {
            next.absorptionDistance = JSONREAD::NumberMemberOr(
                in,
                "absorptionDistance",
                next.absorptionDistance);
        } else if (const nlohmann::json* legacyAbsorption =
                    JSONREAD::FindMember(in, "absorption");
            legacyAbsorption != nullptr && legacyAbsorption->is_number()) {
            const float coefficient = legacyAbsorption->get<float>();
            if (std::isfinite(coefficient) && coefficient > 1.0e-4f) {
                next.absorptionDistance = 1.0f / coefficient;
            }
        }

        if (const nlohmann::json* waves =
                JSONREAD::FindMember(in, "waves");
            waves != nullptr && waves->is_array()) {
            const std::size_t count = (std::min)(
                waves->size(),
                next.waves.size());
            for (std::size_t index = 0; index < count; ++index) {
                const nlohmann::json& source = (*waves)[index];
                if (!source.is_object()) {
                    continue;
                }
                WATER::WaterGerstnerWave& wave = next.waves[index];
                wave.directionDeg = JSONREAD::NumberMemberOr(
                    source,
                    "directionDeg",
                    wave.directionDeg);
                wave.amplitude = JSONREAD::NumberMemberOr(
                    source,
                    "amplitude",
                    wave.amplitude);
                wave.wavelength = JSONREAD::NumberMemberOr(
                    source,
                    "wavelength",
                    wave.wavelength);
                wave.steepness = JSONREAD::NumberMemberOr(
                    source,
                    "steepness",
                    wave.steepness);
            }
        }

        SetSettings(next);
    }

} // namespace HIKARI
