#pragma once

#include <cstdint>
#include <string_view>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    enum class LightComponentType : uint8_t {
        Directional = 0,
        Point,
        Spot,
    };

    class LightComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "LightComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        LightComponentType GetLightType() const noexcept { return type_; }
        const MATH::Vec3& GetColor() const noexcept { return color_; }
        float GetIntensity() const noexcept { return intensity_; }
        float GetRange() const noexcept { return range_; }
        float GetInnerConeAngleDegrees() const noexcept {
            return innerConeAngleDegrees_;
        }
        float GetOuterConeAngleDegrees() const noexcept {
            return outerConeAngleDegrees_;
        }
        float GetAngularFalloff() const noexcept {
            return angularFalloff_;
        }
        int GetPriority() const noexcept { return priority_; }
        uint64_t GetStateRevision() const noexcept {
            return stateRevision_;
        }

        void SetEnabled(bool enabled) noexcept;
        void SetLightType(LightComponentType type) noexcept;
        void SetColor(const MATH::Vec3& color) noexcept;
        void SetIntensity(float intensity) noexcept;
        void SetRange(float range) noexcept;
        void SetSpotConeAnglesDegrees(
            float innerAngleDegrees,
            float outerAngleDegrees) noexcept;
        void SetAngularFalloff(float angularFalloff) noexcept;
        void SetPriority(int priority) noexcept;

    private:
        void ClampSettings() noexcept;
        void BumpStateRevision() noexcept;

        bool enabled_ = true;
        LightComponentType type_ = LightComponentType::Point;
        MATH::Vec3 color_{ 1.0f, 1.0f, 1.0f };
        float intensity_ = 1.0f;
        float range_ = 5.0f;
        float innerConeAngleDegrees_ = 25.0f;
        float outerConeAngleDegrees_ = 35.0f;
        float angularFalloff_ = 1.0f;
        int priority_ = 0;
        uint64_t stateRevision_ = 1u;
    };

} // namespace HIKARI
