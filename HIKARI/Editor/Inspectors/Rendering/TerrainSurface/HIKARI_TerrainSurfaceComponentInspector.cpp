#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <array>

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void TerrainSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {
        constexpr std::array<AuthoringChoice, 3u> kSolidPresets{ {
            { "sand", "Sand" },
            { "snow", "Snow" },
            { "dirt", "Dirt" },
        } };
        if (builder.Section("Terrain Surface", true)) {
            int solidIndex = 0;
            if (surface_.presetId == "snow") { solidIndex = 1; }
            else if (surface_.presetId == "dirt") { solidIndex = 2; }
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        "terrain.solidPreset",
                        "Solid Preset",
                        "Solid surface appearance and deformation preset."
                    },
                    solidIndex,
                    kSolidPresets)) {
                ApplySolidPreset(kSolidPresets[
                    static_cast<std::size_t>(solidIndex)].id);
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.heightScale", "Height Scale",
                    "Overall dent depth scale in meters."
                },
                surface_.heightScale, 0.0f, 2.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeFalloff", "Edge Falloff",
                    "Shoreline transition width in meters."
                },
                surface_.shoreline.edgeFalloff, 0.0f, 40.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeDepth", "Edge Depth",
                    "Shoreline sink depth in meters."
                },
                surface_.shoreline.edgeDepth, 0.0f, 20.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.wetness", "Wetness",
                    "Shoreline wet-sand strength (0 disables)."
                },
                surface_.shoreline.wetness, 0.0f, 1.0f, 0.01f);
        }
        if (builder.Section("Brush", false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.radius", "Radius", "Brush radius in meters."
                },
                surface_.brush.radius, 0.05f, 100.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.strength", "Strength", "Brush strength 0..1."
                },
                surface_.brush.strength, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.hardness", "Hardness", "Inner hardness 0..1."
                },
                surface_.brush.hardness, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.falloff", "Falloff", "Outer soft falloff 0..1."
                },
                surface_.brush.falloff, 0.0f, 1.0f, 0.01f);
        }
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
