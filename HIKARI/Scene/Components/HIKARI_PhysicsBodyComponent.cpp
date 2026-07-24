#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
        constexpr std::array<const char*, 3> kMotionTypeNames{
            "Static", "Kinematic", "Dynamic"
        };

        const char* ToString(PHYSICS::PhysicsMotionType type) noexcept {
            switch (type) {
            case PHYSICS::PhysicsMotionType::Kinematic:
                return "Kinematic";
            case PHYSICS::PhysicsMotionType::Dynamic:
                return "Dynamic";
            case PHYSICS::PhysicsMotionType::Static:
            default:
                return "Static";
            }
        }

        PHYSICS::PhysicsMotionType ParseMotionType(
            const nlohmann::json& value,
            PHYSICS::PhysicsMotionType fallback) {
            if (value.is_number_integer()) {
                const int index = value.get<int>();
                return index >= 0 && index <= 2
                    ? static_cast<PHYSICS::PhysicsMotionType>(index)
                    : fallback;
            }
            if (!value.is_string()) {
                return fallback;
            }
            const std::string text = value.get<std::string>();
            if (text == "Static") return PHYSICS::PhysicsMotionType::Static;
            if (text == "Kinematic") return PHYSICS::PhysicsMotionType::Kinematic;
            if (text == "Dynamic") return PHYSICS::PhysicsMotionType::Dynamic;
            return fallback;
        }

        void DrawVec3(
            IInspectorBuilder& builder,
            std::string_view prefix,
            MATH::Vec3& value) {
            const std::string base(prefix);
            builder.Float(base + " X", value.x);
            builder.Float(base + " Y", value.y);
            builder.Float(base + " Z", value.z);
        }

        void DrawAxisLocks(
            IInspectorBuilder& builder,
            std::string_view prefix,
            MATH::Vec3& axes) {
            bool x = axes.x >= 0.5f;
            bool y = axes.y >= 0.5f;
            bool z = axes.z >= 0.5f;
            const std::string base(prefix);
            builder.Bool(base + " X", x);
            builder.Bool(base + " Y", y);
            builder.Bool(base + " Z", z);
            axes = {
                x ? 1.0f : 0.0f,
                y ? 1.0f : 0.0f,
                z ? 1.0f : 0.0f
            };
        }
    }

    void PhysicsBodyComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["motionType"] = ToString(motionType_);
        out["mass"] = mass_;
        out["gravityScale"] = gravityScale_;
        out["linearDamping"] = linearDamping_;
        out["angularDamping"] = angularDamping_;
        out["allowSleeping"] = allowSleeping_;
        out["continuousCollision"] = continuousCollision_;
        out["initialLinearVelocity"] =
            JsonMath::ToJsonArray(initialLinearVelocity_);
        out["initialAngularVelocity"] =
            JsonMath::ToJsonArray(initialAngularVelocity_);
        out["lockTranslation"] =
            JsonMath::ToJsonArray(lockedTranslationAxes_);
        out["lockRotation"] =
            JsonMath::ToJsonArray(lockedRotationAxes_);
    }

    void PhysicsBodyComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        if (const auto found = in.find("motionType"); found != in.end()) {
            motionType_ = ParseMotionType(*found, motionType_);
        }
        mass_ = in.value("mass", mass_);
        gravityScale_ = in.value("gravityScale", gravityScale_);
        linearDamping_ = in.value("linearDamping", linearDamping_);
        angularDamping_ = in.value("angularDamping", angularDamping_);
        allowSleeping_ = in.value("allowSleeping", allowSleeping_);
        continuousCollision_ = in.value(
            "continuousCollision", continuousCollision_);
        if (in.contains("initialLinearVelocity")) {
            initialLinearVelocity_ = JSONREAD::Vec3Or(
                in["initialLinearVelocity"], initialLinearVelocity_);
        }
        if (in.contains("initialAngularVelocity")) {
            initialAngularVelocity_ = JSONREAD::Vec3Or(
                in["initialAngularVelocity"], initialAngularVelocity_);
        }
        if (in.contains("lockTranslation")) {
            lockedTranslationAxes_ = JSONREAD::Vec3Or(
                in["lockTranslation"], lockedTranslationAxes_);
        }
        if (in.contains("lockRotation")) {
            lockedRotationAxes_ = JSONREAD::Vec3Or(
                in["lockRotation"], lockedRotationAxes_);
        }
        ClampSettings();
    }

    void PhysicsBodyComponent::BuildInspector(
        IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        int motionType = static_cast<int>(motionType_);
        if (builder.Choice("Motion Type", motionType, kMotionTypeNames)) {
            motionType_ = static_cast<PHYSICS::PhysicsMotionType>(
                (std::clamp)(motionType, 0, 2));
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
