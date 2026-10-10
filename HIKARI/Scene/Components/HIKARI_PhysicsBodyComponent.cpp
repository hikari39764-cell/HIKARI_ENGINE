#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"

#include <algorithm>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

namespace JsonMath = SERIALIZATION::JSON::MATH;

void PhysicsBodyComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["motionType"] = TEXT::EnumToName(PHYSICS::kPhysicsMotionTypeNames, motionType_, "Static");
    out["mass"] = mass_;
    out["gravityScale"] = gravityScale_;
    out["linearDamping"] = linearDamping_;
    out["angularDamping"] = angularDamping_;
    out["allowSleeping"] = allowSleeping_;
    out["continuousCollision"] = continuousCollision_;
    out["initialLinearVelocity"] = JsonMath::ToJsonArray(initialLinearVelocity_);
    out["initialAngularVelocity"] = JsonMath::ToJsonArray(initialAngularVelocity_);
    out["lockTranslation"] = JsonMath::ToJsonArray(lockedTranslationAxes_);
    out["lockRotation"] = JsonMath::ToJsonArray(lockedRotationAxes_);
}

void PhysicsBodyComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    TEXT::TryParseEnum(PHYSICS::kPhysicsMotionTypeNames, in.value("motionType", std::string{}), motionType_);
    mass_ = in.value("mass", mass_);
    gravityScale_ = in.value("gravityScale", gravityScale_);
    linearDamping_ = in.value("linearDamping", linearDamping_);
    angularDamping_ = in.value("angularDamping", angularDamping_);
    allowSleeping_ = in.value("allowSleeping", allowSleeping_);
    continuousCollision_ = in.value("continuousCollision", continuousCollision_);
    if (in.contains("initialLinearVelocity")) {
        initialLinearVelocity_ = JSONREAD::Vec3Or(in["initialLinearVelocity"], initialLinearVelocity_);
    }
    if (in.contains("initialAngularVelocity")) {
        initialAngularVelocity_ = JSONREAD::Vec3Or(in["initialAngularVelocity"], initialAngularVelocity_);
    }
    if (in.contains("lockTranslation")) {
        lockedTranslationAxes_ = JSONREAD::Vec3Or(in["lockTranslation"], lockedTranslationAxes_);
    }
    if (in.contains("lockRotation")) {
        lockedRotationAxes_ = JSONREAD::Vec3Or(in["lockRotation"], lockedRotationAxes_);
    }
    ClampSettings();
}

void PhysicsBodyComponent::ClampSettings() noexcept {
    mass_ = (std::max)(mass_, 0.0001f);
    gravityScale_ = (std::clamp)(gravityScale_, -100.0f, 100.0f);
    linearDamping_ = (std::max)(linearDamping_, 0.0f);
    angularDamping_ = (std::max)(angularDamping_, 0.0f);
    const auto normalizeAxes = [](MATH::Vec3& axes) {
        axes.x = axes.x >= 0.5f ? 1.0f : 0.0f;
        axes.y = axes.y >= 0.5f ? 1.0f : 0.0f;
        axes.z = axes.z >= 0.5f ? 1.0f : 0.0f;
    };
    normalizeAxes(lockedTranslationAxes_);
    normalizeAxes(lockedRotationAxes_);
}

bool PhysicsBodyComponent::IsEnabled() const noexcept { return enabled_; }
PHYSICS::PhysicsMotionType PhysicsBodyComponent::GetMotionType() const noexcept { return motionType_; }
float PhysicsBodyComponent::GetMass() const noexcept { return mass_; }
float PhysicsBodyComponent::GetGravityScale() const noexcept { return gravityScale_; }
float PhysicsBodyComponent::GetLinearDamping() const noexcept { return linearDamping_; }
float PhysicsBodyComponent::GetAngularDamping() const noexcept { return angularDamping_; }
bool PhysicsBodyComponent::GetAllowSleeping() const noexcept { return allowSleeping_; }
bool PhysicsBodyComponent::GetContinuousCollision() const noexcept { return continuousCollision_; }
const MATH::Vec3& PhysicsBodyComponent::GetInitialLinearVelocity() const noexcept { return initialLinearVelocity_; }
const MATH::Vec3& PhysicsBodyComponent::GetInitialAngularVelocity() const noexcept { return initialAngularVelocity_; }
const MATH::Vec3& PhysicsBodyComponent::GetLockedTranslationAxes() const noexcept { return lockedTranslationAxes_; }
const MATH::Vec3& PhysicsBodyComponent::GetLockedRotationAxes() const noexcept { return lockedRotationAxes_; }
void PhysicsBodyComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
void PhysicsBodyComponent::SetMotionType(PHYSICS::PhysicsMotionType type) noexcept { motionType_ = type; }

} // namespace HIKARI
