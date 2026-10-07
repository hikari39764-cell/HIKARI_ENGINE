#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <cstdint>
#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

enum class LightComponentType : uint8_t {
    Directional = 0,
    Point,
    Spot,
};

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
    float GetIntensity() const noexcept { return intensity_; }
    float GetRange() const noexcept { return range_; }
    float GetSourceRadius() const noexcept { return sourceRadius_; }
    bool CastsShadows() const noexcept { return castsShadows_; }
    float GetInnerConeAngleDegrees() const noexcept { return innerConeAngleDegrees_; }
    float GetOuterConeAngleDegrees() const noexcept { return outerConeAngleDegrees_; }
    float GetAngularFalloff() const noexcept { return angularFalloff_; }
    RENDER3D::LIGHTING::LocalLightFalloff GetDistanceFalloff() const noexcept { return distanceFalloff_; }
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
    void SetDistanceFalloff(RENDER3D::LIGHTING::LocalLightFalloff value) noexcept;

private:
    void ClampSettings() noexcept;
    void BumpStateRevision() noexcept;

    bool enabled_ = true;
    LightComponentType type_ = LightComponentType::Point;
    MATH::Vec3 color_{1.0f, 1.0f, 1.0f};
    float intensity_ = 1.0f;
    float range_ = 5.0f;
    float sourceRadius_ = 0.0f;
    bool castsShadows_ = true;
    float innerConeAngleDegrees_ = 25.0f;
    float outerConeAngleDegrees_ = 35.0f;
    float angularFalloff_ = 1.0f;
    RENDER3D::LIGHTING::LocalLightFalloff distanceFalloff_ = RENDER3D::LIGHTING::LocalLightFalloff::InverseSquare;
    int priority_ = 0;
    uint64_t stateRevision_ = 1u;
};

} // namespace HIKARI
