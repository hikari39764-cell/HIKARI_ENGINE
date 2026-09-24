#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

namespace {
constexpr std::array<const char*, 3> kMovementSpaces{"World Axes", "Camera Relative", "Object Local"};

void DrawActionValidation(IInspectorBuilder& builder, const INPUT::InputActionMap& actionMap, std::string_view label,
                          const std::string& actionId, INPUT::InputActionValueType expectedType) {
    const INPUT::InputActionDefinition* action = actionMap.FindAction(actionId);
    if (action == nullptr) {
        builder.Text(std::string(label) + " action is missing: " + actionId);
    } else if (action->valueType != expectedType) {
        builder.Text(std::string(label) + " must use " + INPUT::ToString(expectedType) + ", but project action is " +
                     INPUT::ToString(action->valueType) + ".");
    }
}

const char* GroundStateName(PHYSICS::PhysicsCharacterGroundState state) noexcept {
    switch (state) {
    case PHYSICS::PhysicsCharacterGroundState::OnGround: return "Grounded";
    case PHYSICS::PhysicsCharacterGroundState::OnSteepGround: return "Steep Slope";
    case PHYSICS::PhysicsCharacterGroundState::NotSupported: return "Unsupported";
    case PHYSICS::PhysicsCharacterGroundState::InAir:
    default: return "In Air";
    }
}

const char* RuntimeHealthName(PHYSICS::KinematicMotionRuntimeHealth health) noexcept {
    switch (health) {
    case PHYSICS::KinematicMotionRuntimeHealth::Ready: return "Ready";
    case PHYSICS::KinematicMotionRuntimeHealth::RetainedPrevious: return "Previous Solver Retained";
    case PHYSICS::KinematicMotionRuntimeHealth::Error: return "Error";
    case PHYSICS::KinematicMotionRuntimeHealth::Pending:
    default: return "Pending";
    }
}
} // namespace

void CharacterInputComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    builder.Text(TEXT::Localize("character.inputHint"));
    builder.Int("Control Priority", priority_);
    if (builder.Button("Use Gameplay Defaults")) {
        moveActionId_ = "Gameplay.Move";
        lookActionId_ = "Gameplay.Look";
        jumpActionId_ = "Gameplay.Jump";
        sprintActionId_ = "Gameplay.Sprint";
    }
    builder.InputActionIdPicker("Move Action", INPUT::InputActionValueType::Axis2D, moveActionId_);
    builder.InputActionIdPicker("Look Action", INPUT::InputActionValueType::Axis2D, lookActionId_);
    builder.InputActionIdPicker("Jump Action", INPUT::InputActionValueType::Button, jumpActionId_);
    builder.InputActionIdPicker("Sprint Action", INPUT::InputActionValueType::Button, sprintActionId_);

    const InspectorContext& context = builder.GetContext();
    const auto* inputService =
        context.worldServices != nullptr ? context.worldServices->Find<INPUT::InputService>() : nullptr;
    if (inputService != nullptr) {
        const INPUT::InputActionMap& actionMap = inputService->GetActionMap();
        DrawActionValidation(builder, actionMap, "Move", moveActionId_, INPUT::InputActionValueType::Axis2D);
        DrawActionValidation(builder, actionMap, "Look", lookActionId_, INPUT::InputActionValueType::Axis2D);
        DrawActionValidation(builder, actionMap, "Jump", jumpActionId_, INPUT::InputActionValueType::Button);
        DrawActionValidation(builder, actionMap, "Sprint", sprintActionId_, INPUT::InputActionValueType::Button);
    }
}

void CharacterLocomotionComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    builder.Text(TEXT::Localize("character.locomotionHint"));
    if (builder.Section(TEXT::Localize("character.movement"), true)) {
        builder.Int("Motion Priority", motionPriority_);
        int movementSpace = static_cast<int>(movementSpace_);
        if (builder.Choice("Movement Space", movementSpace, kMovementSpaces)) {
            movementSpace_ = static_cast<CharacterMovementSpace>((std::clamp)(movementSpace, 0, 2));
        }
        if (builder.Button("Use Third-Person Movement")) {
            movementSpace_ = CharacterMovementSpace::Camera;
            rotateToMove_ = true;
            edgeGuardEnabled_ = true;
        }
        builder.Float("Maximum Speed", maximumSpeed_);
        builder.Float("Sprint Multiplier", sprintMultiplier_);
        builder.Float("Ground Acceleration", acceleration_);
        builder.Float("Ground Deceleration", deceleration_);
        builder.Float("Air Acceleration", airAcceleration_);
        builder.Float("Air Deceleration", airDeceleration_);
        builder.Float("Turn Speed (deg/s)", turnSpeedDegrees_);
        builder.Bool("Rotate To Move", rotateToMove_);
    }
    if (builder.Section(TEXT::Localize("character.jump"))) {
        builder.Float("Jump Height", jumpHeight_);
        builder.Float("Jump Buffer Time (s)", jumpBufferSeconds_);
        builder.Float("Ground Grace Time (s)", groundGraceSeconds_);
        builder.Float("Maximum Fall Speed", maximumFallSpeed_);
    }
    if (builder.Section(TEXT::Localize("character.traversal"))) {
        builder.FloatRange("Maximum Slope (deg)", maximumSlopeAngleDegrees_, 0.0f, 89.0f, 1.0f);
        builder.Float("Step Height", stepHeight_);
        builder.Float("Floor Snap Distance", stickToFloorDistance_);
        builder.Float("Step Forward Test", stepForwardTestDistance_);
        builder.Bool("Cancel Uphill Motion On Steep Slopes", cancelVelocityTowardsSteepSlopes_);
        builder.Bool("Prevent Grounded Edge Fall", edgeGuardEnabled_);
        builder.Float("Edge Probe Distance", edgeProbeDistance_);
        builder.Float("Edge Probe Radius", edgeProbeRadius_);
        builder.Float("Maximum Grounded Drop", maximumGroundedDrop_);
    }
    if (builder.Section(TEXT::Localize("character.advanced"))) {
        builder.Float("Contact Padding", characterPadding_);
        builder.Float("Predictive Contact Distance", predictiveContactDistance_);
        builder.FloatRange("Penetration Recovery", penetrationRecoverySpeed_, 0.0f, 1.0f, 0.05f);
        int maximumHits = static_cast<int>(maximumCollisionHits_);
        if (builder.Int("Maximum Contact Hits", maximumHits)) {
            maximumCollisionHits_ = static_cast<uint32_t>((std::clamp)(maximumHits, 16, 4096));
        }
        builder.Bool("Smooth Mesh Internal Edges", enhancedInternalEdgeRemoval_);
        builder.FloatRange("Input Dead Zone", inputDeadZone_, 0.0f, 0.95f);
    }
    ClampSettings();
    if (!builder.Section(TEXT::Localize("character.runtime")))
        return;

    const InspectorContext& context = builder.GetContext();
    const auto* motionService =
        context.worldServices != nullptr ? context.worldServices->Find<PHYSICS::KinematicMotionService>() : nullptr;
    const PHYSICS::KinematicMotionState* state = motionService != nullptr && context.runtimeObject != nullptr
                                                     ? motionService->FindState(*context.runtimeObject)
                                                     : nullptr;
    const PHYSICS::KinematicMotionRuntimeStatus* status = motionService != nullptr && context.runtimeObject != nullptr
                                                              ? motionService->FindStatus(*context.runtimeObject)
                                                              : nullptr;
    if (status != nullptr) {
        builder.Text(std::string("Runtime: ") + RuntimeHealthName(status->health) +
                     (status->message.empty() ? std::string{} : " | " + status->message));
    }
    if (state != nullptr) {
        builder.Text(std::string("Ground: ") + GroundStateName(state->groundState));
        builder.Text("Velocity: " + std::to_string(state->velocity.x) + ", " + std::to_string(state->velocity.y) +
                     ", " + std::to_string(state->velocity.z));
        if (state->maximumHitsExceeded) {
            builder.Text("Warning: contact limit reached; simplify nearby collision or raise "
                         "Maximum Contact Hits.");
        }
    }
}

void SpawnPointComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.String("Spawn Point ID", spawnPointId_);
    builder.Bool("Enabled", enabled_);
}

void DialogueInteractorComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    builder.Text("This actor can activate Dialogue Trigger volumes.");
}

void DialogueTriggerComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    builder.AssetIdPicker("Dialogue Asset", AssetType::Dialogue, dialogueAssetGuid_.value);
    builder.String("Locale", locale_);
    int activation = static_cast<int>(activation_);
    constexpr std::array<const char*, 2> activationModes{"On Enter", "Interact"};
    if (builder.Choice("Activation", activation, activationModes)) {
        activation_ = static_cast<DialogueTriggerActivation>(std::clamp(activation, 0, 1));
    }
    if (activation_ == DialogueTriggerActivation::Interact) {
        builder.InputActionIdPicker("Interact Action", INPUT::InputActionValueType::Button, interactActionId_);
    }
    builder.Bool("Pause World During Dialogue", pauseWorld_);
    builder.Bool("One Shot", oneShot_);
    builder.Text(
        "Edit the paired Collider size and center to author the trigger range. The collider must remain a Trigger.");
    if (!dialogueAssetGuid_.IsValid()) {
        builder.Text("Assign a Dialogue asset before play.");
    }
}

} // namespace HIKARI
