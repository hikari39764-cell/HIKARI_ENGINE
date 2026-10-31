#pragma once

#include <cstdint>
#include <string_view>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

enum class CharacterMovementSpace : uint8_t {
    World,
    Camera,
    Object,
};

inline constexpr TEXT::EnumName<CharacterMovementSpace> kCharacterMovementSpaceNames[] = {
    {CharacterMovementSpace::World, "World"},
    {CharacterMovementSpace::Camera, "Camera"},
    {CharacterMovementSpace::Object, "Object"},
};

// 作者値（シーン文書の properties と同じ形）。項目表（kCharacterLocomotionFields）が保存・読み取り・検査器・説明を
// 決める。
struct CharacterLocomotionSettings {
    bool enabled = true;
    int motionPriority = 100;
    CharacterMovementSpace movementSpace = CharacterMovementSpace::Camera;
    float maximumSpeed = 5.0f;
    float sprintMultiplier = 1.5f;
    float acceleration = 30.0f;
    float deceleration = 40.0f;
    float airAcceleration = 10.0f;
    float airDeceleration = 12.0f;
    float turnSpeedDegrees = 720.0f;
    float jumpHeight = 1.2f;
    float jumpBufferSeconds = 0.12f;
    float groundGraceSeconds = 0.10f;
    float maximumFallSpeed = 55.0f;
    float maximumSlopeAngleDegrees = 42.0f;
    float stepHeight = 0.25f;
    float stickToFloorDistance = 0.28f;
    float stepForwardTestDistance = 0.30f;
    float edgeProbeDistance = 0.45f;
    float edgeProbeRadius = 0.18f;
    float maximumGroundedDrop = 0.35f;
    float characterPadding = 0.02f;
    float predictiveContactDistance = 0.1f;
    float penetrationRecoverySpeed = 1.0f;
    uint32_t maximumCollisionHits = 256u;
    bool cancelVelocityTowardsSteepSlopes = true;
    bool edgeGuardEnabled = true;
    bool enhancedInternalEdgeRemoval = true;
    float inputDeadZone = 0.08f;
    bool rotateToMove = true;
};

inline constexpr auto kCharacterLocomotionFields = [](auto&& field) {
    using S = CharacterLocomotionSettings;
    using namespace JSONFIELDS;
    field("enabled", &S::enabled, Label{"Enabled"}, Common{});

    constexpr const char* kMovement = "Movement and facing";
    field("motionPriority", &S::motionPriority, Label{"Motion Priority"}, Group{kMovement});
    field("movementSpace", &S::movementSpace, kCharacterMovementSpaceNames, Label{"Movement Space"},
          Help{"World: fixed world axes. Camera: relative to the active camera. Object: relative to the object's "
               "own orientation."},
          Group{kMovement});
    field("maximumSpeed", &S::maximumSpeed, Range{0.0, 1000.0}, Label{"Maximum Speed"}, Group{kMovement});
    field("sprintMultiplier", &S::sprintMultiplier, Range{1.0, 100.0}, Label{"Sprint Multiplier"}, Group{kMovement});
    field("acceleration", &S::acceleration, Range{0.0, 10000.0}, Label{"Ground Acceleration"}, Group{kMovement});
    field("deceleration", &S::deceleration, Range{0.0, 10000.0}, Label{"Ground Deceleration"}, Group{kMovement});
    field("airAcceleration", &S::airAcceleration, Range{0.0, 10000.0}, Label{"Air Acceleration"}, Group{kMovement});
    field("airDeceleration", &S::airDeceleration, Range{0.0, 10000.0}, Label{"Air Deceleration"}, Group{kMovement});
    field("turnSpeedDegrees", &S::turnSpeedDegrees, Range{0.0, 36000.0}, Label{"Turn Speed"}, Unit{"deg/s"},
          Group{kMovement});
    field("rotateToMove", &S::rotateToMove, Label{"Rotate To Move"}, Group{kMovement});

    constexpr const char* kJump = "Jump and landing";
    field("jumpHeight", &S::jumpHeight, Range{0.0, 1000.0}, Label{"Jump Height"}, Group{kJump});
    field("jumpBufferSeconds", &S::jumpBufferSeconds, Range{0.0, 10.0}, Label{"Jump Buffer Time"}, Unit{"s"},
          Group{kJump});
    field("groundGraceSeconds", &S::groundGraceSeconds, Range{0.0, 10.0}, Label{"Ground Grace Time"}, Unit{"s"},
          Group{kJump});
    field("maximumFallSpeed", &S::maximumFallSpeed, Range{0.0, 10000.0}, Label{"Maximum Fall Speed"}, Group{kJump});

    constexpr const char* kTraversal = "Obstacles and steps";
    field("maximumSlopeAngleDegrees", &S::maximumSlopeAngleDegrees, Slider(0.0, 89.0), Label{"Maximum Slope"},
          Unit{"deg"}, Group{kTraversal});
    field("stepHeight", &S::stepHeight, Range{0.0, 100.0}, Label{"Step Height"}, Group{kTraversal});
    field("stickToFloorDistance", &S::stickToFloorDistance, Range{0.0, 100.0}, Label{"Floor Snap Distance"},
          Group{kTraversal});
    field("stepForwardTestDistance", &S::stepForwardTestDistance, Range{0.0, 100.0}, Label{"Step Forward Test"},
          Group{kTraversal});
    field("cancelVelocityTowardsSteepSlopes", &S::cancelVelocityTowardsSteepSlopes,
          Label{"Cancel Uphill Motion On Steep Slopes"}, Group{kTraversal});
    field("edgeGuardEnabled", &S::edgeGuardEnabled, Label{"Prevent Grounded Edge Fall"}, Group{kTraversal});
    field("edgeProbeDistance", &S::edgeProbeDistance, Range{0.05, 5.0}, Label{"Edge Probe Distance"},
          Group{kTraversal});
    field("edgeProbeRadius", &S::edgeProbeRadius, Range{0.01, 2.0}, Label{"Edge Probe Radius"}, Group{kTraversal});
    field("maximumGroundedDrop", &S::maximumGroundedDrop, Range{0.0, 5.0}, Label{"Maximum Grounded Drop"},
          Group{kTraversal});

    field("characterPadding", &S::characterPadding, Range{0.001, 1.0}, Label{"Contact Padding"}, Advanced{});
    field("predictiveContactDistance", &S::predictiveContactDistance, Range{0.0, 100.0},
          Label{"Predictive Contact Distance"}, Advanced{});
    field("penetrationRecoverySpeed", &S::penetrationRecoverySpeed, Slider(0.0, 1.0), Label{"Penetration Recovery"},
          Advanced{});
    field("maximumCollisionHits", &S::maximumCollisionHits, Range{16.0, 4096.0}, Label{"Maximum Contact Hits"},
          Advanced{});
    field("enhancedInternalEdgeRemoval", &S::enhancedInternalEdgeRemoval, Label{"Smooth Mesh Internal Edges"},
          Advanced{});
    field("inputDeadZone", &S::inputDeadZone, Slider(0.0, 0.95), Label{"Input Dead Zone"}, Advanced{});
};

// 範囲へ丸める。予測接触距離は接触の余白より小さくできない。
CharacterLocomotionSettings NormalizeCharacterLocomotionSettings(CharacterLocomotionSettings settings);

class CharacterLocomotionComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "CharacterLocomotionComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    int GetMotionPriority() const noexcept { return settings_.motionPriority; }
    CharacterMovementSpace GetMovementSpace() const noexcept { return settings_.movementSpace; }
    float GetMaximumSpeed() const noexcept { return settings_.maximumSpeed; }
    float GetSprintMultiplier() const noexcept { return settings_.sprintMultiplier; }
    float GetAcceleration() const noexcept { return settings_.acceleration; }
    float GetDeceleration() const noexcept { return settings_.deceleration; }
    float GetAirAcceleration() const noexcept { return settings_.airAcceleration; }
    float GetAirDeceleration() const noexcept { return settings_.airDeceleration; }
    float GetTurnSpeedRadians() const noexcept;
    float GetJumpHeight() const noexcept { return settings_.jumpHeight; }
    float GetJumpBufferSeconds() const noexcept { return settings_.jumpBufferSeconds; }
    float GetGroundGraceSeconds() const noexcept { return settings_.groundGraceSeconds; }
    float GetMaximumFallSpeed() const noexcept { return settings_.maximumFallSpeed; }
    float GetMaximumSlopeAngleRadians() const noexcept;
    float GetStepHeight() const noexcept { return settings_.stepHeight; }
    float GetStickToFloorDistance() const noexcept { return settings_.stickToFloorDistance; }
    float GetStepForwardTestDistance() const noexcept { return settings_.stepForwardTestDistance; }
    float GetEdgeProbeDistance() const noexcept { return settings_.edgeProbeDistance; }
    float GetEdgeProbeRadius() const noexcept { return settings_.edgeProbeRadius; }
    float GetMaximumGroundedDrop() const noexcept { return settings_.maximumGroundedDrop; }
    bool GetCancelVelocityTowardsSteepSlopes() const noexcept { return settings_.cancelVelocityTowardsSteepSlopes; }
    bool GetEdgeGuardEnabled() const noexcept { return settings_.edgeGuardEnabled; }
    float GetCharacterPadding() const noexcept { return settings_.characterPadding; }
    float GetPredictiveContactDistance() const noexcept { return settings_.predictiveContactDistance; }
    float GetPenetrationRecoverySpeed() const noexcept { return settings_.penetrationRecoverySpeed; }
    uint32_t GetMaximumCollisionHits() const noexcept { return settings_.maximumCollisionHits; }
    bool GetEnhancedInternalEdgeRemoval() const noexcept { return settings_.enhancedInternalEdgeRemoval; }
    float GetInputDeadZone() const noexcept { return settings_.inputDeadZone; }
    bool GetRotateToMove() const noexcept { return settings_.rotateToMove; }

private:
    CharacterLocomotionSettings settings_{};
};

} // namespace HIKARI
