#pragma once

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

namespace HIKARI {

namespace WaterSurfaceComponentPropertyIds {
inline constexpr char Preset[] = "preset";
inline constexpr char GeometryMode[] = "geometryMode";
inline constexpr char TimeScale[] = "timeScale";
inline constexpr char Choppiness[] = "choppiness";
inline constexpr char MicroWaveStrength[] = "microWaveStrength";
inline constexpr char DepthTransmittance[] = "depthTransmittance";
inline constexpr char ScatteringColor[] = "scatteringColor";
inline constexpr char ShallowClearDepth[] = "shallowClearDepth";
inline constexpr char ShallowBlendDepth[] = "shallowBlendDepth";
inline constexpr char ShallowGraphicWeight[] = "shallowGraphicWeight";
inline constexpr char FoamColor[] = "foamColor";
inline constexpr char FoamAmount[] = "foamAmount";
inline constexpr char AbsorptionDistance[] = "absorptionDistance";
inline constexpr char SurfaceRoughness[] = "surfaceRoughness";
inline constexpr char DirectSunStrength[] = "directSunStrength";
inline constexpr char TransmissionStrength[] = "transmissionStrength";
inline constexpr char ShadowStrength[] = "shadowStrength";
inline constexpr char ReflectionStrength[] = "reflectionStrength";
inline constexpr char SunGlintStrength[] = "sunGlintStrength";
inline constexpr char GlintDensity[] = "glintDensity";
inline constexpr char GlintFlowSpeed[] = "glintFlowSpeed";
inline constexpr char RibbonWidth[] = "ribbonWidth";
inline constexpr char WindDirectionDeg[] = "windDirectionDeg";
inline constexpr char WindSpeed[] = "windSpeed";
inline constexpr char ShortWaveStrength[] = "shortWaveStrength";
inline constexpr char CapillaryStrength[] = "capillaryStrength";
} // namespace WaterSurfaceComponentPropertyIds

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
    void NotifyRenderStateDirty();

    WATER::WaterSurfaceSettings settings_{};
};

} // namespace HIKARI
