#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

enum class LightProbeVolumeMode : uint8_t;

class IrradianceVolumeComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "IrradianceVolumeComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return enabled_; }
    LightProbeVolumeMode GetMode() const noexcept { return mode_; }
    const MATH::Vec3& GetSize() const noexcept { return size_; }
    bool SetSize(const MATH::Vec3& size) noexcept;
    uint32_t GetCountX() const noexcept { return countX_; }
    uint32_t GetCountY() const noexcept { return countY_; }
    uint32_t GetCountZ() const noexcept { return countZ_; }
    float GetIntensity() const noexcept { return intensity_; }
    float GetBlendDistance() const noexcept { return blendDistance_; }
    uint32_t GetCaptureResolution() const noexcept { return captureResolution_; }
    int GetPriority() const noexcept { return priority_; }
    const std::string& GetBakeSet() const noexcept { return bakeSet_; }
    const std::string& GetScenario() const noexcept { return scenario_; }

private:
    void ClampSettings() noexcept;

    bool enabled_ = true;
    LightProbeVolumeMode mode_{}; // Baked
    MATH::Vec3 size_{10.0f, 4.0f, 10.0f};
    uint32_t countX_ = 4;
    uint32_t countY_ = 2;
    uint32_t countZ_ = 4;
    float intensity_ = 1.0f;
    float blendDistance_ = 1.0f;
    float normalBias_ = 0.2f;
    uint32_t captureResolution_ = 32;
    uint32_t shOrder_ = 3;
    int priority_ = 0;
    std::string bakeSet_{"Default"};
    std::string scenario_{"Default"};
};

} // namespace HIKARI
