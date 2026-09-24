#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI {

namespace {
// ラベルと説明のローカライズキーから記述子を作る（LocalizeCStr は表の更新まで有効）
AuthoringFieldDescriptor Field(std::string_view id, const char* labelKey, const char* descriptionKey) {
    return {id, TEXT::LocalizeCStr(labelKey), TEXT::LocalizeCStr(descriptionKey)};
}
} // namespace

void TerrainSurfaceComponent::BuildInspector(IInspectorBuilder& builder) {
    bool changed = false;
    const std::array<std::string, 5u> solidPresetLabels{{
        TEXT::Localize("ui.terrain.sand"),
        TEXT::Localize("ui.terrain.snow"),
        TEXT::Localize("ui.terrain.dirt"),
        TEXT::Localize("ui.terrain.grass"),
        TEXT::Localize("ui.terrain.rock"),
    }};
    const std::array<AuthoringChoice, 5u> kSolidPresets{{
        {"sand", solidPresetLabels[0]},
        {"snow", solidPresetLabels[1]},
        {"dirt", solidPresetLabels[2]},
        {"grass", solidPresetLabels[3]},
        {"rock", solidPresetLabels[4]},
    }};
    if (builder.Section(TEXT::Localize("ui.terrain.surface"), true)) {
        int solidIndex = 0;
        if (surface_.presetId == "snow") {
            solidIndex = 1;
        } else if (surface_.presetId == "dirt") {
            solidIndex = 2;
        } else if (surface_.presetId == "grass") {
            solidIndex = 3;
        } else if (surface_.presetId == "rock") {
            solidIndex = 4;
        }
        if (builder.Choice(Field("terrain.solidPreset", "ui.terrain.solidPreset", "ui.terrain.desc.solidPreset"),
                           solidIndex, kSolidPresets)) {
            FillLayer(kSolidPresets[static_cast<std::size_t>(solidIndex)].id);
        }
        changed |=
            builder.FloatRange(Field("terrain.heightScale", "ui.terrain.heightScale", "ui.terrain.desc.heightScale"),
                               surface_.heightScale, 0.0f, 2.0f, 0.01f);
    }
    BuildSurfaceSettingsInspector(builder);
    if (builder.Section(TEXT::Localize("ui.terrain.brush"), false)) {
        changed |= builder.FloatRange(Field("terrain.brush.radius", "ui.terrain.radius", "ui.terrain.desc.radius"),
                                      surface_.brush.radius, 0.05f, 100.0f, 0.1f);
        changed |=
            builder.FloatRange(Field("terrain.brush.strength", "ui.terrain.strength", "ui.terrain.desc.strength"),
                               surface_.brush.strength, 0.0f, TERRAIN::kTerrainBrushMaximumStrength, 0.05f);
        changed |=
            builder.FloatRange(Field("terrain.brush.hardness", "ui.terrain.hardness", "ui.terrain.desc.hardness"),
                               surface_.brush.hardness, 0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.brush.flattenHeight", "ui.terrain.flattenHeight", "ui.terrain.desc.flattenHeight"),
            surface_.brush.flattenHeight, -256.0f, 256.0f, 0.1f);
        changed |= builder.FloatRange(Field("terrain.brush.falloff", "ui.terrain.falloff", "ui.terrain.desc.falloff"),
                                      surface_.brush.falloff, 0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.brush.ridgeDirectionDegrees", "ui.terrain.ridgeDirection", "ui.terrain.desc.ridgeDirection"),
            surface_.brush.ridgeDirectionDegrees, -180.0f, 180.0f, 1.0f);
        changed |= builder.FloatRange(
            Field("terrain.brush.ridgeSharpness", "ui.terrain.ridgeSharpness", "ui.terrain.desc.ridgeSharpness"),
            surface_.brush.ridgeSharpness, 0.05f, 1.0f, 0.01f);
        changed |= builder.FloatRange(Field("terrain.brush.terraceStepHeight", "ui.terrain.terraceStepHeight",
                                            "ui.terrain.desc.terraceStepHeight"),
                                      surface_.brush.terraceStepHeight, 0.05f, 8.0f, 0.05f);
        changed |=
            builder.FloatRange(Field("terrain.brush.shaftDepth", "ui.terrain.shaftDepth", "ui.terrain.desc.shaftDepth"),
                               surface_.brush.shaftDepth, 0.05f, 256.0f, 0.1f);
        changed |= builder.FloatRange(
            Field("terrain.brush.shaftWallWidth", "ui.terrain.shaftWallWidth", "ui.terrain.desc.shaftWallWidth"),
            surface_.brush.shaftWallWidth, 0.01f, 100.0f, 0.05f);
        changed |= builder.FloatRange(
            Field("terrain.brush.noiseAmplitude", "ui.terrain.noiseAmplitude", "ui.terrain.desc.noiseAmplitude"),
            surface_.brush.noiseAmplitude, 0.0f, 16.0f, 0.05f);
        changed |=
            builder.FloatRange(Field("terrain.brush.noiseScale", "ui.terrain.noiseScale", "ui.terrain.desc.noiseScale"),
                               surface_.brush.noiseScale, 0.05f, 512.0f, 0.1f);
    }
    if (changed)
        NotifyRenderStateDirty();
}

bool TerrainSurfaceComponent::BuildSurfaceSettingsInspector(IInspectorBuilder& builder, bool expandGrass) {
    bool anyChanged = false;
    if (builder.Section(TEXT::Localize("ui.terrain.shoreline"), true)) {
        auto shoreline = surface_.shoreline;
        bool changed = false;
        changed |=
            builder.FloatRange(Field("terrain.edgeFalloff", "ui.terrain.edgeFalloff", "ui.terrain.desc.edgeFalloff"),
                               shoreline.edgeFalloff, 0.0f, 40.0f, 0.1f);
        changed |= builder.FloatRange(Field("terrain.edgeDepth", "ui.terrain.edgeDepth", "ui.terrain.desc.edgeDepth"),
                                      shoreline.edgeDepth, 0.0f, 20.0f, 0.1f);
        changed |= builder.FloatRange(Field("terrain.wetness", "ui.terrain.wetness", "ui.terrain.desc.wetness"),
                                      shoreline.wetness, 0.0f, 1.0f, 0.01f);
        if (changed)
            SetShorelineParams(shoreline);
        anyChanged |= changed;
    }
    if (builder.Section(TEXT::Localize("ui.terrain.stylization"), true)) {
        TERRAIN::TerrainStylizationSettings stylization = surface_.stylization;
        bool changed = false;
        changed |= builder.FloatRange(
            Field("terrain.stylization.textureScale", "ui.terrain.textureScale", "ui.terrain.desc.textureScale"),
            stylization.textureScale, 0.1f, 8.0f, 0.01f);
        changed |= builder.FloatRange(Field("terrain.stylization.textureStrength", "ui.terrain.textureStrength",
                                            "ui.terrain.desc.textureStrength"),
                                      stylization.textureStrength, 0.0f, 2.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.normalStrength", "ui.terrain.normalStrength", "ui.terrain.desc.normalStrength"),
            stylization.normalStrength, 0.0f, 2.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.saturation", "ui.terrain.saturation", "ui.terrain.desc.saturation"),
            stylization.saturation, 0.0f, 2.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.shadowStrength", "ui.terrain.shadowStrength", "ui.terrain.desc.shadowStrength"),
            stylization.shadowStrength, 0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.rockSaturation", "ui.terrain.rockSaturation", "ui.terrain.desc.rockSaturation"),
            stylization.rockSaturation, 0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.facetSizeMeters", "ui.terrain.facetSize", "ui.terrain.desc.facetSize"),
            stylization.facetSizeMeters, 0.25f, 16.0f, 0.05f);
        changed |= builder.FloatRange(
            Field("terrain.stylization.facetStrength", "ui.terrain.facetStrength", "ui.terrain.desc.facetStrength"),
            stylization.facetStrength, 0.0f, 1.0f, 0.01f);
        if (changed) {
            SetStylizationSettings(stylization);
            anyChanged = true;
        }
    }
    if (builder.Section(TEXT::Localize("ui.terrain.grass"), expandGrass)) {
        TERRAIN::TerrainGrassSettings grass = surface_.grass;
        bool changed = false;
        changed |= builder.Bool(
            Field("terrain.grass.enabled", "ui.terrain.grassEnabled", "ui.terrain.desc.grassEnabled"), grass.enabled);
        changed |= builder.FloatRange(
            Field("terrain.grass.density", "ui.terrain.grassDensity", "ui.terrain.desc.grassDensity"), grass.density,
            0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.grass.bladeHeight", "ui.terrain.bladeHeight", "ui.terrain.desc.bladeHeight"),
            grass.bladeHeight, 0.005f, 10.0f, 0.005f);
        changed |=
            builder.FloatRange(Field("terrain.grass.bladeWidth", "ui.terrain.bladeWidth", "ui.terrain.desc.bladeWidth"),
                               grass.bladeWidth, 0.001f, 2.0f, 0.001f);
        changed |= builder.FloatRange(Field("terrain.grass.layerWeightThreshold", "ui.terrain.layerWeightThreshold",
                                            "ui.terrain.desc.layerWeightThreshold"),
                                      grass.layerWeightThreshold, 0.0f, 1.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.grass.maxSlopeDegrees", "ui.terrain.maxSlope", "ui.terrain.desc.maxSlope"),
            grass.maxSlopeDegrees, 0.0f, 90.0f, 0.5f);
        changed |= builder.FloatRange(
            Field("terrain.grass.windStrength", "ui.terrain.windStrength", "ui.terrain.desc.windStrength"),
            grass.windStrength, 0.0f, 0.5f, 0.005f);
        changed |=
            builder.FloatRange(Field("terrain.grass.windSpeed", "ui.terrain.windSpeed", "ui.terrain.desc.windSpeed"),
                               grass.windSpeed, 0.0f, 20.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.grass.spatialFrequency", "ui.terrain.spatialFrequency", "ui.terrain.desc.spatialFrequency"),
            grass.spatialFrequency, 0.0f, 20.0f, 0.01f);
        changed |= builder.FloatRange(
            Field("terrain.grass.heightVariation", "ui.terrain.heightVariation", "ui.terrain.desc.heightVariation"),
            grass.heightVariation, 0.0f, 1.0f, 0.01f);
        std::string grassSeed = std::to_string(grass.seed);
        if (builder.String(Field("terrain.grass.seed", "ui.terrain.grassSeed", "ui.terrain.desc.grassSeed"),
                           grassSeed)) {
            uint64_t parsedSeed = 0u;
            const auto parsed = std::from_chars(grassSeed.data(), grassSeed.data() + grassSeed.size(), parsedSeed);
            if (parsed.ec == std::errc{} && parsed.ptr == grassSeed.data() + grassSeed.size()) {
                grass.seed = parsedSeed;
                changed = true;
            }
        }
        int maxClumps = static_cast<int>(grass.maxClumps);
        if (builder.Int(Field("terrain.grass.maxClumps", "ui.terrain.maxClumps", "ui.terrain.desc.maxClumps"),
                        maxClumps)) {
            grass.maxClumps =
                static_cast<uint32_t>((std::clamp)(maxClumps, 0, static_cast<int>(TERRAIN::kTerrainGrassMaxClumps)));
            changed = true;
        }
        if (changed) {
            SetGrassSettings(grass);
            anyChanged = true;
        }
    }
    return anyChanged;
}

} // namespace HIKARI
