#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

namespace {
constexpr std::array<const char*, 3> kMotionTypeNames{"Static", "Kinematic", "Dynamic"};

void DrawVec3(IInspectorBuilder& builder, std::string_view prefix, MATH::Vec3& value) {
    const std::string base(prefix);
    builder.Float(base + " X", value.x);
    builder.Float(base + " Y", value.y);
    builder.Float(base + " Z", value.z);
}

void DrawAxisLocks(IInspectorBuilder& builder, std::string_view prefix, MATH::Vec3& axes) {
    bool x = axes.x >= 0.5f;
    bool y = axes.y >= 0.5f;
    bool z = axes.z >= 0.5f;
    const std::string base(prefix);
    builder.Bool(base + " X", x);
    builder.Bool(base + " Y", y);
    builder.Bool(base + " Z", z);
    axes = {x ? 1.0f : 0.0f, y ? 1.0f : 0.0f, z ? 1.0f : 0.0f};
}
} // namespace

void PhysicsBodyComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    int motionType = static_cast<int>(motionType_);
    if (builder.Choice("Motion Type", motionType, kMotionTypeNames)) {
        motionType_ = static_cast<PHYSICS::PhysicsMotionType>((std::clamp)(motionType, 0, 2));
    }
    builder.Float("Mass", mass_);
    builder.Float("Gravity Scale", gravityScale_);
    builder.Float("Linear Damping", linearDamping_);
    builder.Float("Angular Damping", angularDamping_);
    builder.Bool("Allow Sleeping", allowSleeping_);
    builder.Bool("Continuous Collision", continuousCollision_);
    DrawVec3(builder, "Initial Linear Velocity", initialLinearVelocity_);
    DrawVec3(builder, "Initial Angular Velocity", initialAngularVelocity_);
    DrawAxisLocks(builder, "Lock Translation", lockedTranslationAxes_);
    DrawAxisLocks(builder, "Lock Rotation", lockedRotationAxes_);
    ClampSettings();
}

} // namespace HIKARI
