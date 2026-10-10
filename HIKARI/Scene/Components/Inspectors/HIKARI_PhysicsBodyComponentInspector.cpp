// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"

#include <string>
#include <string_view>

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

namespace {
void DrawAxisLocks(IInspectorBuilder& builder, std::string_view id, std::string_view label, MATH::Vec3& axes) {
    bool locked[3]{axes.x >= 0.5f, axes.y >= 0.5f, axes.z >= 0.5f};
    static constexpr const char* kAxes[3]{"X", "Y", "Z"};
    for (size_t axis = 0; axis < 3u; ++axis) {
        const std::string axisId = std::string(id) + kAxes[axis];
        const std::string axisLabel = std::string(label) + " " + kAxes[axis];
        builder.Bool(AuthoringFieldDescriptor{axisId, axisLabel}, locked[axis]);
    }
    axes = {locked[0] ? 1.0f : 0.0f, locked[1] ? 1.0f : 0.0f, locked[2] ? 1.0f : 0.0f};
}
} // namespace

void PhysicsBodyComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    (void)EnumChoice(builder, AuthoringFieldDescriptor{"motionType", "Motion Type"}, motionType_,
                     PHYSICS::kPhysicsMotionTypeNames);
    builder.Float(AuthoringFieldDescriptor{"mass", "Mass"}, mass_);
    builder.Float(AuthoringFieldDescriptor{"gravityScale", "Gravity Scale"}, gravityScale_);
    builder.Float(AuthoringFieldDescriptor{"linearDamping", "Linear Damping"}, linearDamping_);
    builder.Float(AuthoringFieldDescriptor{"angularDamping", "Angular Damping"}, angularDamping_);
    builder.Bool(AuthoringFieldDescriptor{"allowSleeping", "Allow Sleeping"}, allowSleeping_);
    builder.Bool(AuthoringFieldDescriptor{"continuousCollision", "Continuous Collision"}, continuousCollision_);
    builder.Vec3(AuthoringFieldDescriptor{"initialLinearVelocity", "Initial Linear Velocity"}, initialLinearVelocity_.x,
                 initialLinearVelocity_.y, initialLinearVelocity_.z);
    builder.Vec3(AuthoringFieldDescriptor{"initialAngularVelocity", "Initial Angular Velocity"},
                 initialAngularVelocity_.x, initialAngularVelocity_.y, initialAngularVelocity_.z);
    DrawAxisLocks(builder, "lockTranslation", "Lock Translation", lockedTranslationAxes_);
    DrawAxisLocks(builder, "lockRotation", "Lock Rotation", lockedRotationAxes_);
    ClampSettings();
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
