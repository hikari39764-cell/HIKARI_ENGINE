#pragma once

#include <string>

#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

class ProceduralMeshComponent final : public IComponent, public IGeometryFitProvider {
public:
    std::string_view GetTypeName() const override { return "ProceduralMeshComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const ProceduralMeshSettings& GetSettings() const noexcept;
    void SetSettings(const ProceduralMeshSettings& settings);
    // マテリアル選択: "" = None(デフォルト白) / "checker" または
    // マテリアルアセット GUID。
    const std::string& GetMaterialSelection() const noexcept;
    void SetMaterialSelection(std::string selection);
    int GetGeometryFitPriority() const noexcept override;
    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override;
    // 切り抜いた箱は、残りを重ならない箱の組で返す（衝突を箱だけで正確に作る）。
    void QueryGeometryFitParts(std::vector<GeometryFitDesc>& outParts) const override;

private:
    ProceduralMeshSettings settings_{};
    std::string materialSelection_{};
};

} // namespace HIKARI
