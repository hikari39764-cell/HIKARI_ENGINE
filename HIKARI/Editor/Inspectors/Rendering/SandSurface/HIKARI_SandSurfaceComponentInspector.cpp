#include "Scene/Sand/HIKARI_SandSurfaceComponent.h"

#include <algorithm>

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    void SandSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {

        const SAND::SandSurfaceSettings before = settings_;

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
        }

        settings_ = SAND::SanitizeSandSurfaceSettings(settings_);
        if (!SAND::SandSurfaceSettingsEqual(settings_, before)) {
            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
