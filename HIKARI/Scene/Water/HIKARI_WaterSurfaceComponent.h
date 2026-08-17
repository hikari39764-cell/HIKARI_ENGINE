#pragma once

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

namespace HIKARI {

    namespace WaterSurfaceComponentPropertyIds {
        inline constexpr char Preset[] = "preset";
        inline constexpr char TimeScale[] = "timeScale";
        inline constexpr char Choppiness[] = "choppiness";
        inline constexpr char MicroWaveStrength[] = "microWaveStrength";
        inline constexpr char DepthTransmittance[] = "depthTransmittance";
        inline constexpr char ScatteringColor[] = "scatteringColor";
        inline constexpr char FoamColor[] = "foamColor";
        inline constexpr char FoamAmount[] = "foamAmount";
        inline constexpr char AbsorptionDistance[] = "absorptionDistance";
        inline constexpr char SurfaceRoughness[] = "surfaceRoughness";
    }

    // 独立した水面コンポーネント。TerrainSurface とは分離し、
    // 単一の GameObject が外観と波形だけを保持する。
    class WaterSurfaceComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "WaterSurfaceComponent";
        }

        const WATER::WaterSurfaceSettings& GetSettings() const noexcept;
        void SetSettings(const WATER::WaterSurfaceSettings& settings);
        void ApplyPreset(WATER::WaterSurfacePreset preset);

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        void NotifyRenderStateDirty();

        WATER::WaterSurfaceSettings settings_{};
    };

} // namespace HIKARI
