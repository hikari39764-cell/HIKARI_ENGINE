#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

namespace HIKARI {

inline constexpr auto kWaterGerstnerWaveFields = [](auto&& field) {
    using S = WATER::WaterGerstnerWave;
    using namespace JSONFIELDS;
    field("directionDeg", &S::directionDeg, Range{-180.0, 180.0}, Label{"Direction"}, Unit{"deg"},
          Help{"Travel direction in the water surface's local XZ plane."});
    field("amplitude", &S::amplitude, Range{0.0, 25.0}, Label{"Amplitude"},
          Help{"Vertical displacement in local meters."});
    field("wavelength", &S::wavelength, Range{0.05, 10000.0}, Label{"Wavelength"},
          Help{"Geometric wavelength; unresolved waves are filtered by mesh spacing."});
    field("steepness", &S::steepness, Range{0.0, 1.0}, Label{"Steepness"},
          Help{"Horizontal Gerstner displacement as a fraction of this wave's Amplitude, before global Choppiness "
               "and the shared non-folding slope budget."});
};

// 保存・読み取り・検査器・説明が共有する項目表。範囲は SanitizeWaterSurfaceSettings の丸めと同じ。
inline constexpr auto kWaterSurfaceFields = [](auto&& field) {
    using S = WATER::WaterSurfaceSettings;
    using namespace JSONFIELDS;
    field("preset", &S::preset, WATER::kWaterSurfacePresetNames, Label{"Preset"},
          Help{"Applies one complete summer-water look while keeping every parameter editable."}, Common{});
    field("geometryMode", &S::geometryMode, WATER::kWaterSurfaceGeometryModeNames, Label{"Geometry Mode"},
          Help{"Selects finite authored coverage or an explicit camera-centered ocean geometry provider; presets never "
               "change this mode."},
          Common{});

    constexpr const char* kLook = "Summer Water Preset";
    field("depthTransmittance", &S::depthTransmittance, Range{0.001, 1.0}, Label{"Deep Transmittance"}, Color{},
          Help{"RGB light remaining after one absorption distance; also defines the anime deep-water hue."},
          Group{kLook});
    field("scatteringColor", &S::scatteringColor, Range{0.0, 8.0}, Label{"Shallow Scattering"}, Color{},
          Help{"Water-body scattering color and the anime shallow-water hue."}, Group{kLook});
    field("shallowClearDepth", &S::shallowClearDepth, Range{0.0, 999.999}, Label{"Shallow Clear Depth"},
          Help{"Water depth in meters that remains visually clear before shallow-water blending begins."},
          Group{kLook});
    field("shallowBlendDepth", &S::shallowBlendDepth, Range{0.001, 1000.0}, Label{"Shallow Blend Depth"},
          Help{"Water depth in meters at which the shallow-water color reaches its full blend range."}, Group{kLook});
    field("shallowGraphicWeight", &S::shallowGraphicWeight, Range{0.0, 1.0}, Label{"Shallow Graphic Weight"},
          Help{"Weight of the stylized shallow-water graphic contribution."}, Group{kLook});
    field("foamColor", &S::foamColor, Range{0.0, 8.0}, Label{"Foam Color"}, Color{},
          Help{"Crest and shoreline foam color."}, Group{kLook});
    field("foamAmount", &S::foamAmount, Range{0.0, 2.0}, Label{"Foam Amount"},
          Help{"Controls analytic crest foam and depth-contact foam."}, Group{kLook});
    field("absorptionDistance", &S::absorptionDistance, Range{0.05, 1000.0}, Label{"Absorption Distance"},
          Help{"Distance in meters at which Deep Transmittance is reached."}, Group{kLook});
    field("surfaceRoughness", &S::surfaceRoughness, Range{0.02, 1.0}, Label{"Surface Roughness"},
          Help{"GGX roughness used for sun, local lights, sky, and reflection probes."}, Group{kLook});
    field("directSunStrength", &S::directSunStrength, Range{0.0, 2.0}, Label{"Direct Sun Strength"},
          Help{"Energy of the direct directional-sun specular lobe before shadow influence."}, Group{kLook});
    field("transmissionStrength", &S::transmissionStrength, Range{0.0, 2.0}, Label{"Transmission Strength"},
          Help{"Energy assigned to Beer-Lambert refraction and in-water scattering."}, Group{kLook});
    field("shadowStrength", &S::shadowStrength, Range{0.0, 1.0}, Label{"Shadow Influence"},
          Help{"How strongly directional shadow visibility attenuates direct sun; sky and SSR remain independent."},
          Group{kLook});

    constexpr const char* kSurfaceMotion = "Surface Motion";
    field("timeScale", &S::timeScale, Range{0.0, 4.0}, Label{"Time Scale"},
          Help{"Scales the shared fixed-step simulation clock."}, Group{kSurfaceMotion});
    field("choppiness", &S::choppiness, Range{0.0, 1.0}, Label{"Choppiness"},
          Help{"Controls horizontal Gerstner displacement."}, Group{kSurfaceMotion});
    field("microWaveStrength", &S::microWaveStrength, Range{0.0, 2.0}, Label{"Micro Wave Strength"},
          Help{"Adds procedural shading-normal detail without changing the physics surface."}, Group{kSurfaceMotion});

    constexpr const char* kReflection = "Reflection / Sparkle";
    field("reflectionStrength", &S::reflectionStrength, Range{0.0, 2.0}, Label{"Sky / SSR Reflection"},
          Help{"Energy multiplier for the shared sky and confidence-weighted SSR reflection provider."},
          Group{kReflection});
    field("sunGlintStrength", &S::sunGlintStrength, Range{0.0, 2.0}, Label{"Sun Glitter Strength"},
          Help{"Bounded direct-sun glitter for both formal presets. Anime Summer uses graphic marks; Realistic Clear "
               "Summer uses moving half-vector facets."},
          Group{kReflection});
    field("glintDensity", &S::glintDensity, Range{0.0, 1.0}, Label{"Sun Glitter Density"},
          Help{"Coverage of light-driven sparkle marks or resolved realistic sun facets in water space."},
          Group{kReflection});
    field("glintFlowSpeed", &S::glintFlowSpeed, Range{0.0, 4.0}, Label{"Glint Flow Speed"},
          Help{"Animation rate of the world-anchored glitter phase; the energy still comes from the directional sun."},
          Group{kReflection});
    field("ribbonWidth", &S::ribbonWidth, Range{0.02, 0.5}, Label{"Anime Ribbon Width"},
          Help{"Normalized width of the Anime Summer graphic highlight ribbon; Realistic Clear Summer ignores this "
               "control."},
          Group{kReflection});

    constexpr const char* kMicroWaves = "Ocean Micro Waves";
    field("windDirectionDeg", &S::windDirectionDeg, Range{-180.0, 180.0}, Label{"Wind Direction"}, Unit{"deg"},
          Help{"World-anchored direction of the analytic short-wave slope field."}, Group{kMicroWaves});
    field("windSpeed", &S::windSpeed, Range{0.0, 50.0}, Label{"Wind Speed"},
          Help{"Controls physically bounded short-wave energy and wind-aligned anisotropy; only a bounded drift moves "
               "the field over time."},
          Group{kMicroWaves});
    field("shortWaveStrength", &S::shortWaveStrength, Range{0.0, 2.0}, Label{"Short-Wave Strength"},
          Help{"Amplitude of resolved dynamic short-wave slopes before pixel-footprint filtering."},
          Group{kMicroWaves});
    field("capillaryStrength", &S::capillaryStrength, Range{0.0, 2.0}, Label{"Capillary Strength"},
          Help{"Amplitude of wind-aligned capillary slopes used by the realistic micro-wave lobe."},
          Group{kMicroWaves});

    field("waves", &S::waves, Label{"Wave"}, Elements{kWaterGerstnerWaveFields}, Group{"Geometric Waves"});
};

// 独立した水面コンポーネント。TerrainSurface とは分離し、
// 単一の GameObject が外観と波形だけを保持する。
class WaterSurfaceComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "WaterSurfaceComponent"; }

    const WATER::WaterSurfaceSettings& GetSettings() const noexcept;
    void SetSettings(const WATER::WaterSurfaceSettings& settings);
    void ApplyPreset(WATER::WaterSurfacePreset preset);

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

private:
    // 文書で指定のない項目はプリセットの値（Deserialize と同じ）なので、新しい部品も既定プリセットの値から始める。
    WATER::WaterSurfaceSettings settings_ = WATER::MakeWaterSurfacePresetSettings(WATER::WaterSurfaceSettings{}.preset);
};

} // namespace HIKARI
