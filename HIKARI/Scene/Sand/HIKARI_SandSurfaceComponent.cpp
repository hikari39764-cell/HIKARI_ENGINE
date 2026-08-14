#include "Scene/Sand/HIKARI_SandSurfaceComponent.h"

#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    const SAND::SandSurfaceSettings&
        SandSurfaceComponent::GetSettings() const noexcept {
        return settings_;
    }

    void SandSurfaceComponent::SetSettings(
        const SAND::SandSurfaceSettings& settings) {

        const SAND::SandSurfaceSettings sanitized =
            SAND::SanitizeSandSurfaceSettings(settings);
        if (SAND::SandSurfaceSettingsEqual(settings_, sanitized)) {
            return;
        }
        settings_ = sanitized;
        NotifyRenderStateDirty();
    }

    void SandSurfaceComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void SandSurfaceComponent::Serialize(nlohmann::json& out) const {
        const SAND::SandSurfaceSettings settings =
            SAND::SanitizeSandSurfaceSettings(settings_);
        out = {
            { "_schemaVersion", SAND::kSandSurfaceSchemaVersion },
            { "style", SAND::ToString(settings.style) },
            { "gridResolution", settings.gridResolution },
            { "heightScale", settings.heightScale },
            { "maxDepth", settings.maxDepth },
            { "stampRadius", settings.stampRadius },
            { "rimStrength", settings.rimStrength },
            { "refillPerSecond", settings.refillPerSecond },
            { "diffusionPerSecond", settings.diffusionPerSecond },
            { "lightTint", {
                settings.lightTint.x,
                settings.lightTint.y,
                settings.lightTint.z } },
            { "darkTint", {
                settings.darkTint.x,
                settings.darkTint.y,
                settings.darkTint.z } },
            { "sparkleStrength", settings.sparkleStrength },
            { "rippleAmount", settings.rippleAmount },
            { "pitDarkening", settings.pitDarkening },
            { "toonBandCount", settings.toonBandCount },
            { "toonBandHardness", settings.toonBandHardness },
        };
    }

    void SandSurfaceComponent::Deserialize(const nlohmann::json& in) {
        if (!in.is_object()) {
            return;
        }

        SAND::SandSurfaceSettings next =
            SAND::SanitizeSandSurfaceSettings(settings_);
        next.style = SAND::ParseSandSurfaceStyle(
            in.value(
                "style",
                std::string(SAND::ToString(next.style))),
            next.style);
        next.gridResolution = static_cast<uint32_t>(
            in.value("gridResolution", next.gridResolution));
        next.heightScale =
            in.value("heightScale", next.heightScale);
        next.maxDepth = in.value("maxDepth", next.maxDepth);
        next.stampRadius = in.value("stampRadius", next.stampRadius);
        next.rimStrength = in.value("rimStrength", next.rimStrength);
        next.refillPerSecond =
            in.value("refillPerSecond", next.refillPerSecond);
        next.diffusionPerSecond =
            in.value("diffusionPerSecond", next.diffusionPerSecond);
        if (in.contains("lightTint")) {
            next.lightTint =
                JSONREAD::Vec3Or(in["lightTint"], next.lightTint);
        }
        if (in.contains("darkTint")) {
            next.darkTint =
                JSONREAD::Vec3Or(in["darkTint"], next.darkTint);
        }
        next.sparkleStrength =
            in.value("sparkleStrength", next.sparkleStrength);
        next.rippleAmount =
            in.value("rippleAmount", next.rippleAmount);
        next.pitDarkening =
            in.value("pitDarkening", next.pitDarkening);
        next.toonBandCount = static_cast<uint32_t>(
            in.value("toonBandCount", next.toonBandCount));
        next.toonBandHardness =
            in.value("toonBandHardness", next.toonBandHardness);
        SetSettings(next);
    }

} // namespace HIKARI
