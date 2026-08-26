#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI {

    void TerrainSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {
        const std::array<std::string, 5u> solidPresetLabels{ {
            TEXT::Localize("ui.terrain.sand"),
            TEXT::Localize("ui.terrain.snow"),
            TEXT::Localize("ui.terrain.dirt"),
            TEXT::Localize("ui.terrain.grass"),
            TEXT::Localize("ui.terrain.rock"),
        } };
        const std::array<AuthoringChoice, 5u> kSolidPresets{ {
            { "sand", solidPresetLabels[0] },
            { "snow", solidPresetLabels[1] },
            { "dirt", solidPresetLabels[2] },
            { "grass", solidPresetLabels[3] },
            { "rock", solidPresetLabels[4] },
        } };
        if (builder.Section(TEXT::Localize("ui.terrain.surface"), true)) {
            int solidIndex = 0;
            if (surface_.presetId == "snow") { solidIndex = 1; }
            else if (surface_.presetId == "dirt") { solidIndex = 2; }
            else if (surface_.presetId == "grass") { solidIndex = 3; }
            else if (surface_.presetId == "rock") { solidIndex = 4; }
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        "terrain.solidPreset",
                        TEXT::Localize("ui.terrain.solidPreset"),
                        TEXT::Localize("ui.terrain.desc.solidPreset")
                    },
                    solidIndex,
                    kSolidPresets)) {
                FillLayer(kSolidPresets[
                    static_cast<std::size_t>(solidIndex)].id);
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.heightScale",
                    TEXT::Localize("ui.terrain.heightScale"),
                    TEXT::Localize("ui.terrain.desc.heightScale")
                },
                surface_.heightScale, 0.0f, 2.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeFalloff",
                    TEXT::Localize("ui.terrain.edgeFalloff"),
                    TEXT::Localize("ui.terrain.desc.edgeFalloff")
                },
                surface_.shoreline.edgeFalloff, 0.0f, 40.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeDepth",
                    TEXT::Localize("ui.terrain.edgeDepth"),
                    TEXT::Localize("ui.terrain.desc.edgeDepth")
                },
                surface_.shoreline.edgeDepth, 0.0f, 20.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.wetness",
                    TEXT::Localize("ui.terrain.wetness"),
                    TEXT::Localize("ui.terrain.desc.wetness")
                },
                surface_.shoreline.wetness, 0.0f, 1.0f, 0.01f);
        }
        if (builder.Section(TEXT::Localize("ui.terrain.stylization"), true)) {
            TERRAIN::TerrainStylizationSettings stylization =
                surface_.stylization;
            bool changed = false;
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.textureScale",
                    TEXT::Localize("ui.terrain.textureScale"),
                    TEXT::Localize("ui.terrain.desc.textureScale")
                },
                stylization.textureScale, 0.1f, 8.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.textureStrength",
                    TEXT::Localize("ui.terrain.textureStrength"),
                    TEXT::Localize("ui.terrain.desc.textureStrength")
                },
                stylization.textureStrength, 0.0f, 2.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.normalStrength",
                    TEXT::Localize("ui.terrain.normalStrength"),
                    TEXT::Localize("ui.terrain.desc.normalStrength")
                },
                stylization.normalStrength, 0.0f, 2.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.saturation",
                    TEXT::Localize("ui.terrain.saturation"),
                    TEXT::Localize("ui.terrain.desc.saturation")
                },
                stylization.saturation, 0.0f, 2.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.shadowStrength",
                    TEXT::Localize("ui.terrain.shadowStrength"),
                    TEXT::Localize("ui.terrain.desc.shadowStrength")
                },
                stylization.shadowStrength, 0.0f, 1.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.rockSaturation",
                    TEXT::Localize("ui.terrain.rockSaturation"),
                    TEXT::Localize("ui.terrain.desc.rockSaturation")
                },
                stylization.rockSaturation, 0.0f, 1.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.facetSizeMeters",
                    TEXT::Localize("ui.terrain.facetSize"),
                    TEXT::Localize("ui.terrain.desc.facetSize")
                },
                stylization.facetSizeMeters, 0.25f, 16.0f, 0.05f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.stylization.facetStrength",
                    TEXT::Localize("ui.terrain.facetStrength"),
                    TEXT::Localize("ui.terrain.desc.facetStrength")
                },
                stylization.facetStrength, 0.0f, 1.0f, 0.01f);
            if (changed) {
                SetStylizationSettings(stylization);
            }
        }
        if (builder.Section(TEXT::Localize("ui.terrain.grass"), false)) {
            TERRAIN::TerrainGrassSettings grass = surface_.grass;
            bool changed = false;
            changed |= builder.Bool(
                AuthoringFieldDescriptor{
                    "terrain.grass.enabled",
                    TEXT::Localize("ui.terrain.grassEnabled"),
                    TEXT::Localize("ui.terrain.desc.grassEnabled")
                },
                grass.enabled);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.density",
                    TEXT::Localize("ui.terrain.grassDensity"),
                    TEXT::Localize("ui.terrain.desc.grassDensity")
                },
                grass.density, 0.0f, 1.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.bladeHeight",
                    TEXT::Localize("ui.terrain.bladeHeight"),
                    TEXT::Localize("ui.terrain.desc.bladeHeight")
                },
                grass.bladeHeight, 0.005f, 10.0f, 0.005f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.bladeWidth",
                    TEXT::Localize("ui.terrain.bladeWidth"),
                    TEXT::Localize("ui.terrain.desc.bladeWidth")
                },
                grass.bladeWidth, 0.001f, 2.0f, 0.001f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.layerWeightThreshold",
                    TEXT::Localize("ui.terrain.layerWeightThreshold"),
                    TEXT::Localize("ui.terrain.desc.layerWeightThreshold")
                },
                grass.layerWeightThreshold, 0.0f, 1.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.maxSlopeDegrees",
                    TEXT::Localize("ui.terrain.maxSlope"),
                    TEXT::Localize("ui.terrain.desc.maxSlope")
                },
                grass.maxSlopeDegrees, 0.0f, 90.0f, 0.5f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.windStrength",
                    TEXT::Localize("ui.terrain.windStrength"),
                    TEXT::Localize("ui.terrain.desc.windStrength")
                },
                grass.windStrength, 0.0f, 0.5f, 0.005f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.windSpeed",
                    TEXT::Localize("ui.terrain.windSpeed"),
                    TEXT::Localize("ui.terrain.desc.windSpeed")
                },
                grass.windSpeed, 0.0f, 20.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.spatialFrequency",
                    TEXT::Localize("ui.terrain.spatialFrequency"),
                    TEXT::Localize("ui.terrain.desc.spatialFrequency")
                },
                grass.spatialFrequency, 0.0f, 20.0f, 0.01f);
            changed |= builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.grass.heightVariation",
                    TEXT::Localize("ui.terrain.heightVariation"),
                    TEXT::Localize("ui.terrain.desc.heightVariation")
                },
                grass.heightVariation, 0.0f, 1.0f, 0.01f);
            std::string grassSeed = std::to_string(grass.seed);
            if (builder.String(
                    AuthoringFieldDescriptor{
                        "terrain.grass.seed",
                        TEXT::Localize("ui.terrain.grassSeed"),
                        TEXT::Localize("ui.terrain.desc.grassSeed")
                    },
                    grassSeed)) {
                uint64_t parsedSeed = 0u;
                const auto parsed = std::from_chars(
                    grassSeed.data(),
                    grassSeed.data() + grassSeed.size(),
                    parsedSeed);
                if (parsed.ec == std::errc{} &&
                    parsed.ptr == grassSeed.data() + grassSeed.size()) {
                    grass.seed = parsedSeed;
                    changed = true;
                }
            }
            int maxClumps = static_cast<int>(grass.maxClumps);
            if (builder.Int(
                    AuthoringFieldDescriptor{
                        "terrain.grass.maxClumps",
                        TEXT::Localize("ui.terrain.maxClumps"),
                        TEXT::Localize("ui.terrain.desc.maxClumps")
                    },
                    maxClumps)) {
                grass.maxClumps = static_cast<uint32_t>((std::clamp)(
                    maxClumps,
                    0,
                    static_cast<int>(TERRAIN::kTerrainGrassMaxClumps)));
                changed = true;
            }
            if (changed) {
                SetGrassSettings(grass);
            }
        }
        if (builder.Section(TEXT::Localize("ui.terrain.brush"), false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.radius",
                    TEXT::Localize("ui.terrain.radius"),
                    TEXT::Localize("ui.terrain.desc.radius")
                },
                surface_.brush.radius, 0.05f, 100.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.strength",
                    TEXT::Localize("ui.terrain.strength"),
                    TEXT::Localize("ui.terrain.desc.strength")
                },
                surface_.brush.strength,
                0.0f,
                TERRAIN::kTerrainBrushMaximumStrength,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.hardness",
                    TEXT::Localize("ui.terrain.hardness"),
                    TEXT::Localize("ui.terrain.desc.hardness")
                },
                surface_.brush.hardness, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.flattenHeight",
                    TEXT::Localize("ui.terrain.flattenHeight"),
                    TEXT::Localize("ui.terrain.desc.flattenHeight")
                },
                surface_.brush.flattenHeight,
                -256.0f,
                256.0f,
                0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.falloff",
                    TEXT::Localize("ui.terrain.falloff"),
                    TEXT::Localize("ui.terrain.desc.falloff")
                },
                surface_.brush.falloff, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.ridgeDirectionDegrees",
                    TEXT::Localize("ui.terrain.ridgeDirection"),
                    TEXT::Localize("ui.terrain.desc.ridgeDirection")
                },
                surface_.brush.ridgeDirectionDegrees,
                -180.0f,
                180.0f,
                1.0f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.ridgeSharpness",
                    TEXT::Localize("ui.terrain.ridgeSharpness"),
                    TEXT::Localize("ui.terrain.desc.ridgeSharpness")
                },
                surface_.brush.ridgeSharpness,
                0.05f,
                1.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.terraceStepHeight",
                    TEXT::Localize("ui.terrain.terraceStepHeight"),
                    TEXT::Localize("ui.terrain.desc.terraceStepHeight")
                },
                surface_.brush.terraceStepHeight,
                0.05f,
                8.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.shaftDepth",
                    TEXT::Localize("ui.terrain.shaftDepth"),
                    TEXT::Localize("ui.terrain.desc.shaftDepth")
                },
                surface_.brush.shaftDepth,
                0.05f,
                256.0f,
                0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.shaftWallWidth",
                    TEXT::Localize("ui.terrain.shaftWallWidth"),
                    TEXT::Localize("ui.terrain.desc.shaftWallWidth")
                },
                surface_.brush.shaftWallWidth,
                0.01f,
                100.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.noiseAmplitude",
                    TEXT::Localize("ui.terrain.noiseAmplitude"),
                    TEXT::Localize("ui.terrain.desc.noiseAmplitude")
                },
                surface_.brush.noiseAmplitude,
                0.0f,
                16.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.noiseScale",
                    TEXT::Localize("ui.terrain.noiseScale"),
                    TEXT::Localize("ui.terrain.desc.noiseScale")
                },
                surface_.brush.noiseScale,
                0.05f,
                512.0f,
                0.1f);
        }
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
