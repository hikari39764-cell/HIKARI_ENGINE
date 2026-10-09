#pragma once

#include <cstdint>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"

namespace HIKARI {

// 局所光源。太陽と月は環境プロファイルが持つ。作者値は LightSettings（シーン文書の properties と同じ形）。
class LightComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "LightComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    const LightSettings& GetSettings() const noexcept { return settings_; }
    bool IsEnabled() const noexcept { return settings_.enabled; }
    RENDER3D::LIGHTING::SceneLightType GetLightType() const noexcept { return settings_.type; }
    const MATH::Vec3& GetColor() const noexcept { return settings_.color; }
    // 光束 [lm]。描画は光度 I = Φ / 4π [cd] で、距離の二乗で減衰する。
    float GetIntensity() const noexcept { return settings_.intensity; }
    float GetRange() const noexcept { return settings_.range; }
    float GetSourceRadius() const noexcept { return settings_.sourceRadius; }
    bool CastsShadows() const noexcept { return settings_.castsShadows; }
    float GetInnerConeAngleDegrees() const noexcept { return settings_.innerConeAngleDegrees; }
    float GetOuterConeAngleDegrees() const noexcept { return settings_.outerConeAngleDegrees; }
    float GetAngularFalloff() const noexcept { return settings_.angularFalloff; }
    int GetPriority() const noexcept { return settings_.priority; }
    uint64_t GetStateRevision() const noexcept { return stateRevision_; }

    // 正規化して置き換え、値が変わったら状態の版を進める。
    void SetSettings(LightSettings next) noexcept;

private:
    LightSettings settings_{};
    uint64_t stateRevision_ = 1u;
};

} // namespace HIKARI
