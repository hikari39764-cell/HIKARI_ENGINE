#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"

#include <utility>

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

namespace {
// 範囲へ丸め、軸のロックを 0 か 1 にする。
PhysicsBodySettings Normalize(PhysicsBodySettings settings) {
    settings = JSONFIELDS::Sanitize(std::move(settings), kPhysicsBodyFields);
    const auto normalizeAxes = [](MATH::Vec3& axes) {
        axes.x = axes.x >= 0.5f ? 1.0f : 0.0f;
        axes.y = axes.y >= 0.5f ? 1.0f : 0.0f;
        axes.z = axes.z >= 0.5f ? 1.0f : 0.0f;
    };
    normalizeAxes(settings.lockTranslation);
    normalizeAxes(settings.lockRotation);
    return settings;
}
} // namespace

void PhysicsBodyComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kPhysicsBodyFields);
}

void PhysicsBodyComponent::Deserialize(const nlohmann::json& in) {
    PhysicsBodySettings decoded{};
    JSONFIELDS::Read(in, decoded, kPhysicsBodyFields);
    settings_ = Normalize(std::move(decoded));
}

#if defined(HIKARI_WITH_EDITOR)
void PhysicsBodyComponent::BuildInspector(IInspectorBuilder& builder) {
    PhysicsBodySettings next = settings_;
    if (InspectFields(builder, next, kPhysicsBodyFields)) {
        settings_ = Normalize(std::move(next));
    }
}
#endif

bool PhysicsBodyComponent::IsEnabled() const noexcept { return settings_.enabled; }
PHYSICS::PhysicsMotionType PhysicsBodyComponent::GetMotionType() const noexcept { return settings_.motionType; }
float PhysicsBodyComponent::GetMass() const noexcept { return settings_.mass; }
float PhysicsBodyComponent::GetGravityScale() const noexcept { return settings_.gravityScale; }
float PhysicsBodyComponent::GetLinearDamping() const noexcept { return settings_.linearDamping; }
float PhysicsBodyComponent::GetAngularDamping() const noexcept { return settings_.angularDamping; }
bool PhysicsBodyComponent::GetAllowSleeping() const noexcept { return settings_.allowSleeping; }
bool PhysicsBodyComponent::GetContinuousCollision() const noexcept { return settings_.continuousCollision; }
const MATH::Vec3& PhysicsBodyComponent::GetInitialLinearVelocity() const noexcept {
    return settings_.initialLinearVelocity;
}
const MATH::Vec3& PhysicsBodyComponent::GetInitialAngularVelocity() const noexcept {
    return settings_.initialAngularVelocity;
}
const MATH::Vec3& PhysicsBodyComponent::GetLockedTranslationAxes() const noexcept { return settings_.lockTranslation; }
const MATH::Vec3& PhysicsBodyComponent::GetLockedRotationAxes() const noexcept { return settings_.lockRotation; }
void PhysicsBodyComponent::SetEnabled(bool enabled) noexcept { settings_.enabled = enabled; }
void PhysicsBodyComponent::SetMotionType(PHYSICS::PhysicsMotionType type) noexcept { settings_.motionType = type; }

} // namespace HIKARI
