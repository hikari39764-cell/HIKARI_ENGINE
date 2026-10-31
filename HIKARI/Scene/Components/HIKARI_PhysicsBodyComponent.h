#pragma once

#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kPhysicsBodyFields）が保存・読み取り・検査器・説明を決める。
struct PhysicsBodySettings {
    bool enabled = true;
    PHYSICS::PhysicsMotionType motionType = PHYSICS::PhysicsMotionType::Dynamic;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool allowSleeping = true;
    bool continuousCollision = false;
    MATH::Vec3 initialLinearVelocity{};
    MATH::Vec3 initialAngularVelocity{};
    // 軸ごとに 0（動く）か 1（固定）。
    MATH::Vec3 lockTranslation{};
    MATH::Vec3 lockRotation{};
};

inline constexpr auto kPhysicsBodyFields = [](auto&& field) {
    using S = PhysicsBodySettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"});
    field("motionType", &S::motionType, PHYSICS::kPhysicsMotionTypeNames, Label{"Motion Type"});
    field("mass", &S::mass, Range{0.0001, 1000000.0}, Label{"Mass"});
    field("gravityScale", &S::gravityScale, Range{-100.0, 100.0}, Label{"Gravity Scale"});
    field("linearDamping", &S::linearDamping, Range{0.0, 1000.0}, Label{"Linear Damping"});
    field("angularDamping", &S::angularDamping, Range{0.0, 1000.0}, Label{"Angular Damping"});
    field("allowSleeping", &S::allowSleeping, Label{"Allow Sleeping"});
    field("continuousCollision", &S::continuousCollision, Label{"Continuous Collision"});
    field("initialLinearVelocity", &S::initialLinearVelocity, Label{"Initial Linear Velocity"});
    field("initialAngularVelocity", &S::initialAngularVelocity, Label{"Initial Angular Velocity"});
    field("lockTranslation", &S::lockTranslation, Label{"Lock Translation"}, Draw{FIELD_DRAW::AxisFlags{}});
    field("lockRotation", &S::lockRotation, Label{"Lock Rotation"}, Draw{FIELD_DRAW::AxisFlags{}});
};

class PhysicsBodyComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "PhysicsBodyComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

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
    PhysicsBodySettings settings_{};
};

} // namespace HIKARI
