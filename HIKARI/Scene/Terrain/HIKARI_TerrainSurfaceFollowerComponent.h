#pragma once

#include <string_view>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kTerrainSurfaceFollowerFields）が保存・読み取り・検査器・説明を
// 決める。
struct TerrainSurfaceFollowerSettings {
    // 0 は Collider / ProceduralMesh の球の当てはめから自動で決める。
    float radius = 0.0f;
    float heightOffset = 0.0f;
    float smoothingTime = 0.08f;
};

inline constexpr auto kTerrainSurfaceFollowerFields = [](auto&& field) {
    using S = TerrainSurfaceFollowerSettings;
    using namespace JSONFIELDS;
    constexpr const char* kFit = "Ball Surface Fit";
    field("radius", &S::radius, Range{0.0, 100.0}, Label{"Radius"},
          Help{"Ball radius in local meters. Zero resolves the sphere collider or procedural sphere automatically."},
          Group{kFit});
    field("heightOffset", &S::heightOffset, Range{-100.0, 100.0}, Label{"Height Offset"},
          Help{"Extra vertical offset above the sampled terrain surface height."}, Group{kFit});
    field("smoothingTime", &S::smoothingTime, Range{0.001, 10.0}, Label{"Smoothing Time"},
          Help{"How fast the visual ball settles onto the surface."}, Group{kFit});
};

class TerrainSurfaceFollowerComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "TerrainSurfaceFollowerComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    void OnDetach() override;
    void Update(float dt) override;

    // for imprint system: resolved radius (Collider/mesh sphere fit).
    float ResolveRadius() const noexcept;

private:
    TerrainSurfaceFollowerSettings settings_{};
    float smoothedHeight_ = 0.0f;
    bool smoothedHeightValid_ = false;
};

} // namespace HIKARI
