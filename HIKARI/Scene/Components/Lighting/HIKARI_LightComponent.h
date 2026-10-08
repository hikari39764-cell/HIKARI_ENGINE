#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstdint>
#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 局所光源。太陽と月は環境プロファイルが持つ。
enum class LightComponentType : uint8_t {
    Point,
    Spot,
};

// 光束 [lm] の上限と既定値（白熱電球 60W 相当）。
inline constexpr float kMaximumLightLumens = 1000000.0f;
inline constexpr float kDefaultLightLumens = 800.0f;

class LightComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "LightComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return enabled_; }
    LightComponentType GetLightType() const noexcept { return type_; }
    const MATH::Vec3& GetColor() const noexcept { return color_; }
    // 光束 [lm]。描画は光度 I = Φ / 4π [cd] で、距離の二乗で減衰する。
    float GetIntensity() const noexcept { return intensity_; }
    float GetRange() const noexcept { return range_; }
    float GetSourceRadius() const noexcept { return sourceRadius_; }
    bool CastsShadows() const noexcept { return castsShadows_; }
    float GetInnerConeAngleDegrees() const noexcept { return innerConeAngleDegrees_; }
    float GetOuterConeAngleDegrees() const noexcept { return outerConeAngleDegrees_; }
    float GetAngularFalloff() const noexcept { return angularFalloff_; }
    int GetPriority() const noexcept { return priority_; }
    uint64_t GetStateRevision() const noexcept { return stateRevision_; }

    void SetEnabled(bool enabled) noexcept;
    void SetLightType(LightComponentType type) noexcept;
    void SetColor(const MATH::Vec3& color) noexcept;
    void SetIntensity(float intensity) noexcept;
    void SetRange(float range) noexcept;
    void SetSourceRadius(float radius) noexcept;
    void SetCastsShadows(bool value) noexcept;
    void SetSpotConeAnglesDegrees(float innerAngleDegrees, float outerAngleDegrees) noexcept;
    void SetAngularFalloff(float angularFalloff) noexcept;

private:
    void ClampSettings() noexcept;
    void BumpStateRevision() noexcept;

    bool enabled_ = true;
    LightComponentType type_ = LightComponentType::Point;
    MATH::Vec3 color_{1.0f, 1.0f, 1.0f};
    float intensity_ = kDefaultLightLumens;
    float range_ = 10.0f;
    float sourceRadius_ = 0.0f;
    bool castsShadows_ = true;
    float innerConeAngleDegrees_ = 25.0f;
    float outerConeAngleDegrees_ = 35.0f;
    float angularFalloff_ = 1.0f;
    int priority_ = 0;
    uint64_t stateRevision_ = 1u;
};

} // namespace HIKARI
