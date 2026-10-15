#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeContract.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

enum class ReflectionProbeViewportBoundsTarget : uint8_t {
    Influence,
    Projection,
};

inline constexpr TEXT::EnumName<ReflectionProbeViewportBoundsTarget> kReflectionProbeViewportBoundsTargetNames[] = {
    {ReflectionProbeViewportBoundsTarget::Influence, "Influence"},
    {ReflectionProbeViewportBoundsTarget::Projection, "Projection"},
};
inline constexpr TEXT::EnumName<ReflectionProbeInfluenceShape> kReflectionProbeInfluenceShapeNames[] = {
    {ReflectionProbeInfluenceShape::Sphere, "Sphere"},
    {ReflectionProbeInfluenceShape::Box, "Box"},
};
inline constexpr TEXT::EnumName<ReflectionProbeProjectionShape> kReflectionProbeProjectionShapeNames[] = {
    {ReflectionProbeProjectionShape::Infinite, "Infinite"},
    {ReflectionProbeProjectionShape::Box, "Box"},
};

class ReflectionProbeComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "ReflectionProbeComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return enabled_; }
    float GetRadius() const noexcept { return radius_; }
    bool SetRadius(float radius) noexcept;
    float GetIntensity() const noexcept { return intensity_; }
    ReflectionProbeInfluenceShape GetInfluenceShape() const noexcept { return influenceShape_; }
    const MATH::Vec3& GetInfluenceBoxOffset() const noexcept { return influenceBoxOffset_; }
    const MATH::Vec3& GetInfluenceBoxSize() const noexcept { return influenceBoxSize_; }
    bool SetInfluenceBoxSize(const MATH::Vec3& size) noexcept;
    bool GetProjectionFollowsInfluence() const noexcept { return projectionFollowsInfluence_; }
    void SetProjectionFollowsInfluence(bool enabled) noexcept;
    ReflectionProbeProjectionShape GetProjectionShape() const noexcept { return projectionShape_; }
    const MATH::Vec3& GetProjectionBoxOffset() const noexcept { return projectionBoxOffset_; }
    const MATH::Vec3& GetProjectionBoxSize() const noexcept { return projectionBoxSize_; }
    bool SetProjectionBoxSize(const MATH::Vec3& size) noexcept;
    void AlignProjectionToInfluence() noexcept;
    ReflectionProbeViewportBoundsTarget GetViewportBoundsTarget() const noexcept { return viewportBoundsTarget_; }
    float GetBlendDistance() const noexcept { return blendDistance_; }
    int GetPriority() const noexcept { return priority_; }
    uint32_t GetCaptureResolution() const noexcept { return captureResolution_; }
    const std::string& GetBakeSet() const noexcept { return bakeSet_; }
    const std::string& GetScenario() const noexcept { return scenario_; }

private:
    void ClampSettings() noexcept;

    bool enabled_ = true;
    std::string sourceCubemapAsset_{};
    float radius_ = 8.0f;
    float intensity_ = 1.0f;
    ReflectionProbeInfluenceShape influenceShape_ = ReflectionProbeInfluenceShape::Sphere;
    MATH::Vec3 influenceBoxOffset_{0.0f, 0.0f, 0.0f};
    MATH::Vec3 influenceBoxSize_{8.0f, 4.0f, 8.0f};
    ReflectionProbeProjectionShape projectionShape_ = ReflectionProbeProjectionShape::Infinite;
    MATH::Vec3 projectionBoxOffset_{0.0f, 0.0f, 0.0f};
    MATH::Vec3 projectionBoxSize_{8.0f, 4.0f, 8.0f};
    bool projectionFollowsInfluence_ = true;
    // Authoring session state. It is intentionally not serialized.
    ReflectionProbeViewportBoundsTarget viewportBoundsTarget_ = ReflectionProbeViewportBoundsTarget::Influence;
    float blendDistance_ = 1.0f;
    int priority_ = 0;
    uint32_t captureResolution_ = 128;
    std::string bakeSet_{"Default"};
    std::string scenario_{"Default"};
};

} // namespace HIKARI
