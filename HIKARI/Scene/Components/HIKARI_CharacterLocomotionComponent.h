#pragma once

#include <cstdint>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    enum class CharacterMovementSpace : uint8_t {
        World,
        Camera,
        Object,
    };

    class CharacterLocomotionComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "CharacterLocomotionComponent";
        }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        int GetMotionPriority() const noexcept { return motionPriority_; }
        CharacterMovementSpace GetMovementSpace() const noexcept {
            return movementSpace_;
        }
        float GetMaximumSpeed() const noexcept { return maximumSpeed_; }
        float GetSprintMultiplier() const noexcept {
            return sprintMultiplier_;
        }
        float GetAcceleration() const noexcept { return acceleration_; }
        float GetDeceleration() const noexcept { return deceleration_; }
        float GetAirAcceleration() const noexcept {
            return airAcceleration_;
        }
        float GetAirDeceleration() const noexcept {
            return airDeceleration_;
        }
        float GetTurnSpeedRadians() const noexcept;
        float GetJumpHeight() const noexcept { return jumpHeight_; }
        float GetJumpBufferSeconds() const noexcept {
            return jumpBufferSeconds_;
        }
        float GetGroundGraceSeconds() const noexcept {
            return groundGraceSeconds_;
        }
        float GetMaximumFallSpeed() const noexcept {
            return maximumFallSpeed_;
        }
        float GetMaximumSlopeAngleRadians() const noexcept;
        float GetStepHeight() const noexcept { return stepHeight_; }
        float GetStickToFloorDistance() const noexcept {
            return stickToFloorDistance_;
        }
        float GetStepForwardTestDistance() const noexcept {
            return stepForwardTestDistance_;
        }
        float GetCharacterPadding() const noexcept {
            return characterPadding_;
        }
        float GetPredictiveContactDistance() const noexcept {
            return predictiveContactDistance_;
        }
        float GetPenetrationRecoverySpeed() const noexcept {
            return penetrationRecoverySpeed_;
        }
        uint32_t GetMaximumCollisionHits() const noexcept {
            return maximumCollisionHits_;
        }
        bool GetEnhancedInternalEdgeRemoval() const noexcept {
            return enhancedInternalEdgeRemoval_;
        }
        float GetInputDeadZone() const noexcept { return inputDeadZone_; }
        bool GetRotateToMove() const noexcept { return rotateToMove_; }

    private:
        void ClampSettings() noexcept;

        bool enabled_ = true;
        int motionPriority_ = 100;
        CharacterMovementSpace movementSpace_ =
            CharacterMovementSpace::Camera;
        float maximumSpeed_ = 5.0f;
        float sprintMultiplier_ = 1.5f;
        float acceleration_ = 30.0f;
        float deceleration_ = 40.0f;
        float airAcceleration_ = 10.0f;
        float airDeceleration_ = 12.0f;
        float turnSpeedDegrees_ = 720.0f;
        float jumpHeight_ = 1.2f;
        float jumpBufferSeconds_ = 0.12f;
        float groundGraceSeconds_ = 0.10f;
        float maximumFallSpeed_ = 55.0f;
        float maximumSlopeAngleDegrees_ = 50.0f;
        float stepHeight_ = 0.4f;
        float stickToFloorDistance_ = 0.5f;
        float stepForwardTestDistance_ = 0.15f;
        float characterPadding_ = 0.02f;
        float predictiveContactDistance_ = 0.1f;
        float penetrationRecoverySpeed_ = 1.0f;
        uint32_t maximumCollisionHits_ = 256u;
        bool enhancedInternalEdgeRemoval_ = true;
        float inputDeadZone_ = 0.08f;
        bool rotateToMove_ = true;
    };

} // namespace HIKARI
