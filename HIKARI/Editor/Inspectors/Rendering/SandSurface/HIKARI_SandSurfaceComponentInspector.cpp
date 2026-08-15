#include "Scene/Sand/HIKARI_SandSurfaceComponent.h"

#include <algorithm>
#include <array>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {

    void SandSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {

        const SAND::SandSurfaceSettings before = settings_;
        const SAND::SandSurfaceTerrain beforeTerrain = terrain_;

        if (builder.Section("Deformation", true)) {
            int resolution =
                static_cast<int>(settings_.gridResolution);
            if (builder.Int(
                    AuthoringFieldDescriptor{
                        SandSurfaceComponentPropertyIds::GridResolution,
                        "Grid Resolution",
                        "CPU/GPU height-field node count per axis. "
                        "Higher values make softer, tighter ruts."
                    },
                    resolution)) {
                settings_.gridResolution = static_cast<uint32_t>(
                    std::max(resolution, 0));
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::HeightScale,
                    "Height Scale",
                    "Scales procedural dunes and interaction depth."
                },
                settings_.heightScale,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::MaxDepth,
                    "Max Depth",
                    "Maximum stamp depth in local meters."
                },
                settings_.maxDepth,
                0.01f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::StampRadius,
                    "Stamp Radius",
                    "Contact depression radius in local meters."
                },
                settings_.stampRadius,
                0.02f,
                10.0f,
                0.02f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::RimStrength,
                    "Rim Strength",
                    "Sand pushed up around the pit edge."
                },
                settings_.rimStrength,
                0.0f,
                2.0f,
                0.01f);
        }

        if (builder.Section("Settling", false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::RefillPerSecond,
                    "Refill / Second",
                    "How fast pits decay back toward the base dunes."
                },
                settings_.refillPerSecond,
                0.0f,
                10.0f,
                0.005f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::DiffusionPerSecond,
                    "Diffusion / Second",
                    "Lateral smoothing that keeps deformation soft."
                },
                settings_.diffusionPerSecond,
                0.0f,
                100.0f,
                0.1f);
        }

        if (builder.Section("Anime Look", true)) {
            builder.Color3(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::LightTint,
                    "Light Tint",
                    "Lit side of the two-tone sand ramp."
                },
                settings_.lightTint.x,
                settings_.lightTint.y,
                settings_.lightTint.z);
            builder.Color3(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::DarkTint,
                    "Dark Tint",
                    "Shadow side of the two-tone sand ramp."
                },
                settings_.darkTint.x,
                settings_.darkTint.y,
                settings_.darkTint.z);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::SparkleStrength,
                    "Grain Sparkle",
                    "High-frequency glint strength on sun-lit grains."
                },
                settings_.sparkleStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::RippleAmount,
                    "Wind Ripples",
                    "Procedural wind-ripple strength on the base surface."
                },
                settings_.rippleAmount,
                0.0f,
                3.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::PitDarkening,
                    "Pit Darkening",
                    "Wet/deep tint inside fresh depressions."
                },
                settings_.pitDarkening,
                0.0f,
                1.0f,
                0.01f);
            int toonBands =
                static_cast<int>(settings_.toonBandCount);
            if (builder.Int(
                    AuthoringFieldDescriptor{
                        SandSurfaceComponentPropertyIds::ToonBandCount,
                        "Toon Bands",
                        "Number of flat cel shading bands (2-4)."
                    },
                    toonBands)) {
                settings_.toonBandCount = static_cast<uint32_t>(
                    std::clamp(toonBands, 2, 4));
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::ToonBandHardness,
                    "Band Hardness",
                    "0 is a soft gradient, 0.5 gives hard cel edges."
                },
                settings_.toonBandHardness,
                0.0f,
                0.5f,
                0.01f);
        }

        if (builder.Section("Terrain", true)) {
            int terrainResolution =
                static_cast<int>(terrain_.authoringResolution);
            if (builder.Int(
                    AuthoringFieldDescriptor{
                        SandSurfaceComponentPropertyIds::TerrainResolution,
                        "Terrain Resolution",
                        "Authored terrain height-field nodes per axis. "
                        "Lower values are lighter to serialize."
                    },
                    terrainResolution)) {
                terrain_.authoringResolution = static_cast<uint32_t>(
                    std::clamp(terrainResolution, 16, 128));
            }

            int brushMode = static_cast<int>(terrain_.brush.mode);
            static constexpr std::array<const char*, 4> kBrushModes{
                "Raise",
                "Lower",
                "Smooth",
                "Flatten"
            };
            if (builder.Choice("Brush Mode", brushMode, kBrushModes)) {
                terrain_.brush.mode =
                    static_cast<SAND::SandSurfaceBrushMode>(
                        std::clamp(brushMode, 0, 3));
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::BrushRadius,
                    "Brush Radius",
                    "Sculpt brush radius in local meters."
                },
                terrain_.brush.radius,
                0.1f,
                20.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::BrushStrength,
                    "Brush Strength",
                    "Height change per stroke in local meters."
                },
                terrain_.brush.strength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::BrushHardness,
                    "Brush Hardness",
                    "0 is a soft falloff, 1 is a hard edge."
                },
                terrain_.brush.hardness,
                0.0f,
                1.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    SandSurfaceComponentPropertyIds::BrushFlattenHeight,
                    "Flatten Height",
                    "Target height in local meters for the Flatten brush."
                },
                terrain_.brush.flattenHeight,
                -5.0f,
                5.0f,
                0.01f);

            if (builder.Button("Reset Terrain")) {
                ResetTerrain();
            }
            if (builder.Button("Bake Dune Height")) {
                if (const auto* mesh =
                        builder.GetContext().proceduralMeshSettings) {
                    BakeTerrain({
                        mesh->width * 0.5f,
                        mesh->depth * 0.5f
                    });
                }
            }
        }

        settings_ = SAND::SanitizeSandSurfaceSettings(settings_);
        terrain_ = SAND::SanitizeSandSurfaceTerrain(terrain_);
        if (!SAND::SandSurfaceSettingsEqual(settings_, before)) {
            NotifyRenderStateDirty();
        }
        if (!SAND::SandSurfaceTerrainEqual(terrain_, beforeTerrain)) {
            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
