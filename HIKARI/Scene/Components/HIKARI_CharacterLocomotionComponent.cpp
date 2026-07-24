#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"

#include <algorithm>
#include <array>
#include <string>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {
    namespace {
        constexpr float kDegreesToRadians = 0.01745329252f;
        constexpr std::array<const char*, 3> kMovementSpaces{
            "World Axes", "Camera Relative", "Object Local"
        };

        const char* ToString(CharacterMovementSpace space) noexcept {
            switch (space) {
            case CharacterMovementSpace::World: return "World";
            case CharacterMovementSpace::Object: return "Object";
            case CharacterMovementSpace::Camera:
            default: return "Camera";
            }
        }

        CharacterMovementSpace ParseMovementSpace(
            const nlohmann::json& value,
            CharacterMovementSpace fallback) {
            if (!value.is_string()) {
                return fallback;
            }
            const std::string text = value.get<std::string>();
            if (text == "World") return CharacterMovementSpace::World;
            if (text == "Object") return CharacterMovementSpace::Object;
            if (text == "Camera") return CharacterMovementSpace::Camera;
            return fallback;
        }

        const char* GroundStateName(
            PHYSICS::PhysicsCharacterGroundState state) noexcept {
            switch (state) {
            case PHYSICS::PhysicsCharacterGroundState::OnGround:
                return "Grounded";
            case PHYSICS::PhysicsCharacterGroundState::OnSteepGround:
                return "Steep Slope";
            case PHYSICS::PhysicsCharacterGroundState::NotSupported:
                return "Unsupported";
            case PHYSICS::PhysicsCharacterGroundState::InAir:
            default:
                return "In Air";
            }
        }

        const char* RuntimeHealthName(
            PHYSICS::KinematicMotionRuntimeHealth health) noexcept {
            switch (health) {
            case PHYSICS::KinematicMotionRuntimeHealth::Ready:
                return "Ready";
            case PHYSICS::KinematicMotionRuntimeHealth::RetainedPrevious:
                return "Previous Solver Retained";
            case PHYSICS::KinematicMotionRuntimeHealth::Error:
                return "Error";
            case PHYSICS::KinematicMotionRuntimeHealth::Pending:
            default:
                return "Pending";
            }
        }
    }

    void CharacterLocomotionComponent::Serialize(
        nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["motionPriority"] = motionPriority_;
        out["movementSpace"] = ToString(movementSpace_);
        out["maximumSpeed"] = maximumSpeed_;
        out["sprintMultiplier"] = sprintMultiplier_;
        out["acceleration"] = acceleration_;
        out["deceleration"] = deceleration_;
        out["airAcceleration"] = airAcceleration_;
        out["airDeceleration"] = airDeceleration_;
        out["turnSpeedDegrees"] = turnSpeedDegrees_;
        out["jumpHeight"] = jumpHeight_;
        out["jumpBufferSeconds"] = jumpBufferSeconds_;
        out["groundGraceSeconds"] = groundGraceSeconds_;
        out["maximumFallSpeed"] = maximumFallSpeed_;
        out["maximumSlopeAngleDegrees"] = maximumSlopeAngleDegrees_;
        out["stepHeight"] = stepHeight_;
        out["stickToFloorDistance"] = stickToFloorDistance_;
        out["stepForwardTestDistance"] = stepForwardTestDistance_;
        out["characterPadding"] = characterPadding_;
        out["predictiveContactDistance"] =
            predictiveContactDistance_;
        out["penetrationRecoverySpeed"] =
            penetrationRecoverySpeed_;
        out["maximumCollisionHits"] = maximumCollisionHits_;
        out["enhancedInternalEdgeRemoval"] =
            enhancedInternalEdgeRemoval_;
        out["inputDeadZone"] = inputDeadZone_;
        out["rotateToMove"] = rotateToMove_;
    }

    void CharacterLocomotionComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        motionPriority_ = in.value("motionPriority", motionPriority_);
        if (const auto found = in.find("movementSpace");
            found != in.end()) {
            movementSpace_ = ParseMovementSpace(*found, movementSpace_);
        }
        maximumSpeed_ = in.value("maximumSpeed", maximumSpeed_);
        sprintMultiplier_ = in.value(
            "sprintMultiplier", sprintMultiplier_);
        acceleration_ = in.value("acceleration", acceleration_);
        deceleration_ = in.value("deceleration", deceleration_);
        airAcceleration_ = in.value(
            "airAcceleration", airAcceleration_);
        airDeceleration_ = in.value(
            "airDeceleration", airDeceleration_);
        turnSpeedDegrees_ = in.value(
            "turnSpeedDegrees", turnSpeedDegrees_);
        jumpHeight_ = in.value("jumpHeight", jumpHeight_);
        jumpBufferSeconds_ = in.value(
            "jumpBufferSeconds", jumpBufferSeconds_);
        groundGraceSeconds_ = in.value(
            "groundGraceSeconds", groundGraceSeconds_);
        maximumFallSpeed_ = in.value(
            "maximumFallSpeed", maximumFallSpeed_);
        maximumSlopeAngleDegrees_ = in.value(
            "maximumSlopeAngleDegrees", maximumSlopeAngleDegrees_);
        stepHeight_ = in.value("stepHeight", stepHeight_);
        stickToFloorDistance_ = in.value(
            "stickToFloorDistance", stickToFloorDistance_);
        stepForwardTestDistance_ = in.value(
            "stepForwardTestDistance", stepForwardTestDistance_);
        characterPadding_ = in.value(
            "characterPadding", characterPadding_);
        predictiveContactDistance_ = in.value(
            "predictiveContactDistance", predictiveContactDistance_);
        penetrationRecoverySpeed_ = in.value(
            "penetrationRecoverySpeed", penetrationRecoverySpeed_);
        maximumCollisionHits_ = in.value(
            "maximumCollisionHits", maximumCollisionHits_);
        enhancedInternalEdgeRemoval_ = in.value(
            "enhancedInternalEdgeRemoval",
            enhancedInternalEdgeRemoval_);
        inputDeadZone_ = in.value("inputDeadZone", inputDeadZone_);
        rotateToMove_ = in.value("rotateToMove", rotateToMove_);
        ClampSettings();
    }

    void CharacterLocomotionComponent::BuildInspector(
        IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Int("Motion Priority", motionPriority_);
        int movementSpace = static_cast<int>(movementSpace_);
        if (builder.Choice(
                "Movement Space",
                movementSpace,
                kMovementSpaces)) {
            movementSpace_ = static_cast<CharacterMovementSpace>(
                (std::clamp)(movementSpace, 0, 2));
        }
        if (builder.Button("Use Third-Person Movement")) {
            movementSpace_ = CharacterMovementSpace::Camera;
            rotateToMove_ = true;
        }
        builder.Float("Maximum Speed", maximumSpeed_);
        builder.Float("Sprint Multiplier", sprintMultiplier_);
        builder.Float("Ground Acceleration", acceleration_);
        builder.Float("Ground Deceleration", deceleration_);
        builder.Float("Air Acceleration", airAcceleration_);
        builder.Float("Air Deceleration", airDeceleration_);
        builder.Float("Turn Speed (deg/s)", turnSpeedDegrees_);
        builder.Float("Jump Height", jumpHeight_);
        builder.Float("Jump Buffer Time (s)", jumpBufferSeconds_);
        builder.Float("Ground Grace Time (s)", groundGraceSeconds_);
        builder.Float("Maximum Fall Speed", maximumFallSpeed_);
        builder.FloatRange(
            "Maximum Slope (deg)",
            maximumSlopeAngleDegrees_,
            0.0f,
            89.0f,
            1.0f);
        builder.Float("Step Height", stepHeight_);
        builder.Float("Floor Snap Distance", stickToFloorDistance_);
        builder.Float("Step Forward Test", stepForwardTestDistance_);
        builder.Float("Contact Padding", characterPadding_);
        builder.Float(
            "Predictive Contact Distance",
            predictiveContactDistance_);
        builder.FloatRange(
            "Penetration Recovery",
            penetrationRecoverySpeed_,
            0.0f,
            1.0f,
            0.05f);
        int maximumHits = static_cast<int>(maximumCollisionHits_);
        if (builder.Int("Maximum Contact Hits", maximumHits)) {
            maximumCollisionHits_ = static_cast<uint32_t>(
                (std::clamp)(maximumHits, 16, 4096));
        }
        builder.Bool(
            "Smooth Mesh Internal Edges",
            enhancedInternalEdgeRemoval_);
        builder.FloatRange("Input Dead Zone", inputDeadZone_, 0.0f, 0.95f);
        builder.Bool("Rotate To Move", rotateToMove_);
        ClampSettings();

        const InspectorContext& context = builder.GetContext();
        const auto* motionService = context.worldServices != nullptr
            ? context.worldServices->Find<
                PHYSICS::KinematicMotionService>()
            : nullptr;
        const PHYSICS::KinematicMotionState* state =
            motionService != nullptr && context.runtimeObject != nullptr
            ? motionService->FindState(*context.runtimeObject)
            : nullptr;
        const PHYSICS::KinematicMotionRuntimeStatus* status =
            motionService != nullptr && context.runtimeObject != nullptr
            ? motionService->FindStatus(*context.runtimeObject)
            : nullptr;
        if (status != nullptr) {
            builder.Text(
                std::string("Runtime: ") +
                RuntimeHealthName(status->health) +
                (status->message.empty()
                    ? std::string{}
                    : " | " + status->message));
        }
        if (state != nullptr) {
            builder.Text(
                std::string("Ground: ") +
                GroundStateName(state->groundState));
            builder.Text(
                "Velocity: " + std::to_string(state->velocity.x) +
                ", " + std::to_string(state->velocity.y) +
                ", " + std::to_string(state->velocity.z));
            if (state->maximumHitsExceeded) {
                builder.Text(
                    "Warning: contact limit reached; simplify nearby collision or raise Maximum Contact Hits.");
            }
        }
    }

    void CharacterLocomotionComponent::ClampSettings() noexcept {
        maximumSpeed_ = (std::max)(maximumSpeed_, 0.0f);
        sprintMultiplier_ = (std::max)(sprintMultiplier_, 1.0f);
        acceleration_ = (std::max)(acceleration_, 0.0f);
        deceleration_ = (std::max)(deceleration_, 0.0f);
        airAcceleration_ = (std::max)(airAcceleration_, 0.0f);
        airDeceleration_ = (std::max)(airDeceleration_, 0.0f);
        turnSpeedDegrees_ = (std::max)(turnSpeedDegrees_, 0.0f);
        jumpHeight_ = (std::max)(jumpHeight_, 0.0f);
        jumpBufferSeconds_ = (std::max)(jumpBufferSeconds_, 0.0f);
        groundGraceSeconds_ = (std::max)(groundGraceSeconds_, 0.0f);
        maximumFallSpeed_ = (std::max)(maximumFallSpeed_, 0.0f);
        maximumSlopeAngleDegrees_ = (std::clamp)(
            maximumSlopeAngleDegrees_, 0.0f, 89.0f);
        stepHeight_ = (std::max)(stepHeight_, 0.0f);
        stickToFloorDistance_ = (std::max)(
            stickToFloorDistance_, 0.0f);
        stepForwardTestDistance_ = (std::max)(
            stepForwardTestDistance_, 0.0f);
        characterPadding_ = (std::clamp)(
            characterPadding_, 0.001f, 1.0f);
        predictiveContactDistance_ = (std::max)(
            predictiveContactDistance_, characterPadding_);
        penetrationRecoverySpeed_ = (std::clamp)(
            penetrationRecoverySpeed_, 0.0f, 1.0f);
        maximumCollisionHits_ = (std::clamp)(
            maximumCollisionHits_, 16u, 4096u);
        inputDeadZone_ = (std::clamp)(inputDeadZone_, 0.0f, 0.95f);
    }

    float CharacterLocomotionComponent::GetTurnSpeedRadians()
        const noexcept {
        return turnSpeedDegrees_ * kDegreesToRadians;
    }

    float CharacterLocomotionComponent::GetMaximumSlopeAngleRadians()
        const noexcept {
        return maximumSlopeAngleDegrees_ * kDegreesToRadians;
    }

} // namespace HIKARI
