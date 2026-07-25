#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"

#include <algorithm>
#include <string>

namespace HIKARI {
    namespace {
        constexpr float kDegreesToRadians = 0.01745329252f;

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
