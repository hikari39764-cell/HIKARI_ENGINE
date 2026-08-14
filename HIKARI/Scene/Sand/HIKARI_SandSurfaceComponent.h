#pragma once

#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"

namespace HIKARI {

    namespace SandSurfaceComponentPropertyIds {
        inline constexpr char Style[] = "style";
        inline constexpr char GridResolution[] = "gridResolution";
        inline constexpr char HeightScale[] = "heightScale";
        inline constexpr char MaxDepth[] = "maxDepth";
        inline constexpr char StampRadius[] = "stampRadius";
        inline constexpr char RimStrength[] = "rimStrength";
        inline constexpr char RefillPerSecond[] = "refillPerSecond";
        inline constexpr char DiffusionPerSecond[] = "diffusionPerSecond";
        inline constexpr char LightTint[] = "lightTint";
        inline constexpr char DarkTint[] = "darkTint";
        inline constexpr char SparkleStrength[] = "sparkleStrength";
        inline constexpr char RippleAmount[] = "rippleAmount";
        inline constexpr char PitDarkening[] = "pitDarkening";
        inline constexpr char ToonBandCount[] = "toonBandCount";
        inline constexpr char ToonBandHardness[] = "toonBandHardness";
    }

    class SandSurfaceComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "SandSurfaceComponent";
        }

        const SAND::SandSurfaceSettings& GetSettings() const noexcept;
        void SetSettings(const SAND::SandSurfaceSettings& settings);

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        void NotifyRenderStateDirty();

        SAND::SandSurfaceSettings settings_{};
    };

} // namespace HIKARI
