#pragma once

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    class PhysicsBodyComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "PhysicsBodyComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept;
        PHYSICS::PhysicsMotionType GetMotionType() const noexcept;
        float GetMass() const noexcept;
        float GetGravityScale() const noexcept;
        float GetLinearDamping() const noexcept;
        float GetAngularDamping() const noexcept;
        bool GetAllowSleeping() const noexcept;
        bool GetContinuousCollision() const noexcept;
        const MATH::Vec3& GetInitialLinearVelocity() const noexcept;
        const MATH::Vec3& GetInitialAngularVelocity() const noexcept;
        const MATH::Vec3& GetLockedTranslationAxes() const noexcept;
        const MATH::Vec3& GetLockedRotationAxes() const noexcept;

        void SetEnabled(bool enabled) noexcept;
        void SetMotionType(PHYSICS::PhysicsMotionType type) noexcept;

    private:
        void ClampSettings() noexcept;

        bool enabled_ = true;
        PHYSICS::PhysicsMotionType motionType_ =
            PHYSICS::PhysicsMotionType::Dynamic;
        float mass_ = 1.0f;
        float gravityScale_ = 1.0f;
        float linearDamping_ = 0.05f;
        float angularDamping_ = 0.05f;
        bool allowSleeping_ = true;
        bool continuousCollision_ = false;
        MATH::Vec3 initialLinearVelocity_{};
        MATH::Vec3 initialAngularVelocity_{};
        MATH::Vec3 lockedTranslationAxes_{};
        MATH::Vec3 lockedRotationAxes_{};
    };

} // namespace HIKARI
