#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

#include <array>
#include <string>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    namespace {
        constexpr std::array<AuthoringChoice, 2> kWaterPresets{{
            { "anime_summer", "Anime Summer" },
            { "realistic_clear_summer", "Realistic Clear Summer" },
        }};
        constexpr std::array<AuthoringChoice, 2> kWaterGeometryModes{{
            { "finite_authored", "Finite Authored" },
            { "camera_centered_ocean", "Camera-Centered Ocean" },
        }};

        constexpr std::array<const char*, 4> kWaveDirectionIds{{
            "waves.0.directionDeg",
            "waves.1.directionDeg",
            "waves.2.directionDeg",
            "waves.3.directionDeg",
        }};
        constexpr std::array<const char*, 4> kWaveAmplitudeIds{{
            "waves.0.amplitude",
            "waves.1.amplitude",
            "waves.2.amplitude",
            "waves.3.amplitude",
        }};
        constexpr std::array<const char*, 4> kWaveLengthIds{{
            "waves.0.wavelength",
            "waves.1.wavelength",
            "waves.2.wavelength",
            "waves.3.wavelength",
        }};
        constexpr std::array<const char*, 4> kWaveSteepnessIds{{
            "waves.0.steepness",
            "waves.1.steepness",
            "waves.2.steepness",
            "waves.3.steepness",
        }};
    }

    void WaterSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {

        const WATER::WaterSurfaceSettings before = settings_;

        if (builder.Section("Summer Water Preset", true)) {
            int preset = static_cast<int>(settings_.preset);
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        WaterSurfaceComponentPropertyIds::Preset,
                        "Preset",
                        "Applies one complete summer-water look while keeping every parameter editable."
                    },
                    preset,
                    kWaterPresets)) {
                const WATER::WaterSurfacePreset selectedPreset =
                    preset == static_cast<int>(
                        WATER::WaterSurfacePreset::RealisticClearSummer)
                        ? WATER::WaterSurfacePreset::RealisticClearSummer
                        : WATER::WaterSurfacePreset::AnimeSummer;
                const WATER::WaterSurfaceGeometryMode geometryMode =
                    settings_.geometryMode;
                settings_ = WATER::MakeWaterSurfacePresetSettings(
                    selectedPreset);
                // Preset is an optical/motion bundle; geometry ownership is
                // an independent authoring choice and must survive A/B swaps.
                settings_.geometryMode = geometryMode;
            }
            builder.Color3(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::DepthTransmittance,
                    "Deep Transmittance",
                    "RGB light remaining after one absorption distance; also defines the anime deep-water hue."
                },
                settings_.depthTransmittance.x,
                settings_.depthTransmittance.y,
                settings_.depthTransmittance.z);
            builder.Color3(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ScatteringColor,
                    "Shallow Scattering",
                    "Water-body scattering color and the anime shallow-water hue."
                },
                settings_.scatteringColor.x,
                settings_.scatteringColor.y,
                settings_.scatteringColor.z);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ShallowClearDepth,
                    "Shallow Clear Depth",
                    "Water depth in meters that remains visually clear before shallow-water blending begins."
                },
                settings_.shallowClearDepth,
                0.0f,
                100.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ShallowBlendDepth,
                    "Shallow Blend Depth",
                    "Water depth in meters at which the shallow-water color reaches its full blend range."
                },
                settings_.shallowBlendDepth,
                0.001f,
                100.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ShallowGraphicWeight,
                    "Shallow Graphic Weight",
                    "Weight of the stylized shallow-water graphic contribution."
                },
                settings_.shallowGraphicWeight,
                0.0f,
                1.0f,
                0.01f);
            builder.Color3(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::FoamColor,
                    "Foam Color",
                    "Crest and shoreline foam color."
                },
                settings_.foamColor.x,
                settings_.foamColor.y,
                settings_.foamColor.z);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::FoamAmount,
                    "Foam Amount",
                    "Controls analytic crest foam and depth-contact foam."
                },
                settings_.foamAmount,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::AbsorptionDistance,
                    "Absorption Distance",
                    "Distance in meters at which Deep Transmittance is reached."
                },
                settings_.absorptionDistance,
                0.05f,
                100.0f,
                0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::SurfaceRoughness,
                    "Surface Roughness",
                    "GGX roughness used for sun, local lights, sky, and reflection probes."
                },
                settings_.surfaceRoughness,
                0.02f,
                1.0f,
                0.005f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::DirectSunStrength,
                    "Direct Sun Strength",
                    "Energy of the direct directional-sun specular lobe before shadow influence."
                },
                settings_.directSunStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::TransmissionStrength,
                    "Transmission Strength",
                    "Energy assigned to Beer-Lambert refraction and in-water scattering."
                },
                settings_.transmissionStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ShadowStrength,
                    "Shadow Influence",
                    "How strongly directional shadow visibility attenuates direct sun; sky and SSR remain independent."
                },
                settings_.shadowStrength,
                0.0f,
                1.0f,
                0.01f);
        }

        if (builder.Section("Geometry / Extent", false)) {
            int geometryMode = static_cast<int>(settings_.geometryMode);
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        WaterSurfaceComponentPropertyIds::GeometryMode,
                        "Geometry Mode",
                        "Selects finite authored coverage or an explicit camera-centered ocean geometry provider; presets never change this mode."
                    },
                    geometryMode,
                    kWaterGeometryModes)) {
                settings_.geometryMode = geometryMode == static_cast<int>(
                    WATER::WaterSurfaceGeometryMode::CameraCenteredOcean)
                    ? WATER::WaterSurfaceGeometryMode::CameraCenteredOcean
                    : WATER::WaterSurfaceGeometryMode::FiniteAuthored;
            }
        }

        if (builder.Section("Reflection / Sparkle", false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ReflectionStrength,
                    "Sky / SSR Reflection",
                    "Energy multiplier for the shared sky and confidence-weighted SSR reflection provider."
                },
                settings_.reflectionStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::SunGlintStrength,
                    "Sun Glitter Strength",
                    "Bounded direct-sun glitter for both formal presets. Anime Summer uses graphic marks; Realistic Clear Summer uses moving half-vector facets."
                },
                settings_.sunGlintStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::GlintDensity,
                    "Sun Glitter Density",
                    "Coverage of light-driven sparkle marks or resolved realistic sun facets in water space."
                },
                settings_.glintDensity,
                0.0f,
                1.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::GlintFlowSpeed,
                    "Glint Flow Speed",
                    "Animation rate of the world-anchored glitter phase; the energy still comes from the directional sun."
                },
                settings_.glintFlowSpeed,
                0.0f,
                4.0f,
                0.02f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::RibbonWidth,
                    "Anime Ribbon Width",
                    "Normalized width of the Anime Summer graphic highlight ribbon; Realistic Clear Summer ignores this control."
                },
                settings_.ribbonWidth,
                0.02f,
                0.5f,
                0.005f);
        }

        if (builder.Section("Ocean Micro Waves", false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::WindDirectionDeg,
                    "Wind Direction (deg)",
                    "World-anchored direction of the analytic short-wave slope field."
                },
                settings_.windDirectionDeg,
                -180.0f,
                180.0f,
                1.0f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::WindSpeed,
                    "Wind Speed",
                    "Controls physically bounded short-wave energy and wind-aligned anisotropy; only a bounded drift moves the field over time."
                },
                settings_.windSpeed,
                0.0f,
                50.0f,
                0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::ShortWaveStrength,
                    "Short-Wave Strength",
                    "Amplitude of resolved dynamic short-wave slopes before pixel-footprint filtering."
                },
                settings_.shortWaveStrength,
                0.0f,
                2.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::CapillaryStrength,
                    "Capillary Strength",
                    "Amplitude of wind-aligned capillary slopes used by the realistic micro-wave lobe."
                },
                settings_.capillaryStrength,
                0.0f,
                2.0f,
                0.01f);
        }

        if (builder.Section("Surface Motion", true)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::TimeScale,
                    "Time Scale",
                    "Scales the shared fixed-step simulation clock."
                },
                settings_.timeScale,
                0.0f,
                4.0f,
                0.02f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::Choppiness,
                    "Choppiness",
                    "Controls horizontal Gerstner displacement."
                },
                settings_.choppiness,
                0.0f,
                1.0f,
                0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    WaterSurfaceComponentPropertyIds::MicroWaveStrength,
                    "Micro Wave Strength",
                    "Adds procedural shading-normal detail without changing the physics surface."
                },
                settings_.microWaveStrength,
                0.0f,
                2.0f,
                0.01f);
        }

        if (builder.Section("Geometric Waves", false)) {
            for (std::size_t index = 0u;
                index < settings_.waves.size();
                ++index) {
                WATER::WaterGerstnerWave& wave = settings_.waves[index];
                const std::string section =
                    "Wave " + std::to_string(index + 1u);
                if (!builder.Section(section, index == 0u)) {
                    continue;
                }
                builder.FloatRange(
                    AuthoringFieldDescriptor{
                        kWaveDirectionIds[index],
                        "Direction (deg)",
                        "Travel direction in the water surface's local XZ plane."
                    },
                    wave.directionDeg,
                    -180.0f,
                    180.0f,
                    1.0f);
                builder.FloatRange(
                    AuthoringFieldDescriptor{
                        kWaveAmplitudeIds[index],
                        "Amplitude",
                        "Vertical displacement in local meters."
                    },
                    wave.amplitude,
                    0.0f,
                    4.0f,
                    0.01f);
                builder.FloatRange(
                    AuthoringFieldDescriptor{
                        kWaveLengthIds[index],
                        "Wavelength",
                        "Geometric wavelength; unresolved waves are filtered by mesh spacing."
                    },
                    wave.wavelength,
                    0.05f,
                    200.0f,
                    0.1f);
                builder.FloatRange(
                    AuthoringFieldDescriptor{
                        kWaveSteepnessIds[index],
                        "Steepness",
                        "Horizontal Gerstner displacement as a fraction of this wave's Amplitude, before global Choppiness and the shared non-folding slope budget."
                    },
                    wave.steepness,
                    0.0f,
                    1.0f,
                    0.01f);
            }
        }

        settings_ = WATER::SanitizeWaterSurfaceSettings(settings_);
        if (!WATER::WaterSurfaceSettingsEqual(settings_, before)) {
            NotifyRenderStateDirty();
        }
    }

} // namespace HIKARI
