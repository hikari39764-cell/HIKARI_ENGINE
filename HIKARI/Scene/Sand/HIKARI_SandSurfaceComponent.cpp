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

    const SAND::SandSurfaceTerrain&
        SandSurfaceComponent::GetTerrain() const noexcept {
        return terrain_;
    }

    void SandSurfaceComponent::SetTerrain(
        const SAND::SandSurfaceTerrain& terrain) {

        const SAND::SandSurfaceTerrain sanitized =
            SAND::SanitizeSandSurfaceTerrain(terrain);
        if (SAND::SandSurfaceTerrainEqual(terrain_, sanitized)) {
            return;
        }
        terrain_ = sanitized;
        NotifyRenderStateDirty();
    }

    void SandSurfaceComponent::SculptTerrain(
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) {

        SAND::SandSurfaceTerrain next = terrain_;
        SAND::SculptSandSurfaceTerrain(next, halfExtents, localXZ);
        SetTerrain(next);
    }

    void SandSurfaceComponent::ResetTerrain() {
        SAND::SandSurfaceTerrain next = terrain_;
        SAND::ResetSandSurfaceTerrain(next);
        SetTerrain(next);
    }

    void SandSurfaceComponent::BakeTerrain(
        const MATH::Vec2& halfExtents) {

        SAND::SandSurfaceTerrain next = terrain_;
        SAND::BakeSandSurfaceTerrain(
            next,
            halfExtents,
            settings_.rippleAmount,
            settings_.heightScale);
        SetTerrain(next);
    }

    float SandSurfaceComponent::SampleAuthoredHeight(
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) const noexcept {

        return SAND::SampleSandSurfaceTerrain(
            terrain_,
            halfExtents,
            localXZ);
    }

    void SandSurfaceComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void SandSurfaceComponent::Serialize(nlohmann::json& out) const {
        const SAND::SandSurfaceSettings settings =
            SAND::SanitizeSandSurfaceSettings(settings_);
        const SAND::SandSurfaceTerrain terrain =
            SAND::SanitizeSandSurfaceTerrain(terrain_);
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
            { "terrainResolution", terrain.authoringResolution },
            { "brushMode", SAND::ToString(terrain.brush.mode) },
            { "brushRadius", terrain.brush.radius },
            { "brushStrength", terrain.brush.strength },
            { "brushHardness", terrain.brush.hardness },
            { "brushFlattenHeight", terrain.brush.flattenHeight },
            {
                "terrainHeights",
                SAND::EncodeSandSurfaceTerrainHeights(terrain)
            },
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
        SAND::SandSurfaceTerrain terrain =
            SAND::SanitizeSandSurfaceTerrain(terrain_);
        terrain.authoringResolution = static_cast<uint32_t>(
            in.value(
                "terrainResolution",
                terrain.authoringResolution));
        terrain.brush.mode = SAND::ParseSandSurfaceBrushMode(
            in.value(
                "brushMode",
                std::string(SAND::ToString(terrain.brush.mode))),
            terrain.brush.mode);
        terrain.brush.radius =
            in.value("brushRadius", terrain.brush.radius);
        terrain.brush.strength =
            in.value("brushStrength", terrain.brush.strength);
        terrain.brush.hardness =
            in.value("brushHardness", terrain.brush.hardness);
        terrain.brush.flattenHeight =
            in.value(
                "brushFlattenHeight",
                terrain.brush.flattenHeight);
        if (in.contains("terrainHeights")) {
            if (in["terrainHeights"].is_string()) {
                (void)SAND::DecodeSandSurfaceTerrainHeights(
                    terrain,
                    in["terrainHeights"].get<std::string>());
            } else if (in["terrainHeights"].is_array()) {
                terrain.heights.clear();
                terrain.heights.reserve(
                    in["terrainHeights"].size());
                for (const nlohmann::json& value :
                    in["terrainHeights"]) {
                    terrain.heights.push_back(
                        value.is_number()
                            ? value.get<float>()
                            : 0.0f);
                }
            }
        }
        SetTerrain(terrain);
        SetSettings(next);
    }

} // namespace HIKARI
