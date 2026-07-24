#pragma once

#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

    class ProceduralMeshComponent final :
        public IComponent,
        public IGeometryFitProvider {
    public:
        std::string_view GetTypeName() const override {
            return "ProceduralMeshComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        const ProceduralMeshSettings& GetSettings() const noexcept;
        void SetSettings(const ProceduralMeshSettings& settings);
        int GetGeometryFitPriority() const noexcept override;
        bool QueryGeometryFit(
            GeometryFitDesc& outFit) const noexcept override;

    private:
        void NotifyRenderStateDirty();

        ProceduralMeshSettings settings_{};
    };

} // namespace HIKARI
