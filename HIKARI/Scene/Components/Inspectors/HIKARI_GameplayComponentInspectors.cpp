// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

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
constexpr std::array<std::string_view, std::size(kCharacterMovementSpaceNames)> kMovementSpaceLabels{
    "World Axes", "Camera Relative", "Object Local"};

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
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    builder.Text(TEXT::Localize("character.inputHint"));
    builder.Int(AuthoringFieldDescriptor{"priority", "Control Priority"}, priority_);
    if (builder.Button("Use Gameplay Defaults")) {
        moveActionId_ = "Gameplay.Move";
        lookActionId_ = "Gameplay.Look";
        jumpActionId_ = "Gameplay.Jump";
        sprintActionId_ = "Gameplay.Sprint";
    }
    builder.InputActionIdPicker(AuthoringFieldDescriptor{"moveAction", "Move Action"},
                                INPUT::InputActionValueType::Axis2D, moveActionId_);
    builder.InputActionIdPicker(AuthoringFieldDescriptor{"lookAction", "Look Action"},
                                INPUT::InputActionValueType::Axis2D, lookActionId_);
    builder.InputActionIdPicker(AuthoringFieldDescriptor{"jumpAction", "Jump Action"},
                                INPUT::InputActionValueType::Button, jumpActionId_);
    builder.InputActionIdPicker(AuthoringFieldDescriptor{"sprintAction", "Sprint Action"},
                                INPUT::InputActionValueType::Button, sprintActionId_);

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
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    builder.Text(TEXT::Localize("character.locomotionHint"));
    if (builder.Section(TEXT::Localize("character.movement"), true)) {
        builder.Int(AuthoringFieldDescriptor{"motionPriority", "Motion Priority"}, motionPriority_);
        (void)EnumChoice(builder, AuthoringFieldDescriptor{"movementSpace", "Movement Space"}, movementSpace_,
                         kCharacterMovementSpaceNames, &kMovementSpaceLabels);
        if (builder.Button("Use Third-Person Movement")) {
            movementSpace_ = CharacterMovementSpace::Camera;
            rotateToMove_ = true;
            edgeGuardEnabled_ = true;
        }
        builder.Float(AuthoringFieldDescriptor{"maximumSpeed", "Maximum Speed"}, maximumSpeed_);
        builder.Float(AuthoringFieldDescriptor{"sprintMultiplier", "Sprint Multiplier"}, sprintMultiplier_);
        builder.Float(AuthoringFieldDescriptor{"acceleration", "Ground Acceleration"}, acceleration_);
        builder.Float(AuthoringFieldDescriptor{"deceleration", "Ground Deceleration"}, deceleration_);
        builder.Float(AuthoringFieldDescriptor{"airAcceleration", "Air Acceleration"}, airAcceleration_);
        builder.Float(AuthoringFieldDescriptor{"airDeceleration", "Air Deceleration"}, airDeceleration_);
        builder.Float(AuthoringFieldDescriptor{"turnSpeedDegrees", "Turn Speed (deg/s)"}, turnSpeedDegrees_);
        builder.Bool(AuthoringFieldDescriptor{"rotateToMove", "Rotate To Move"}, rotateToMove_);
    }
    if (builder.Section(TEXT::Localize("character.jump"))) {
        builder.Float(AuthoringFieldDescriptor{"jumpHeight", "Jump Height"}, jumpHeight_);
        builder.Float(AuthoringFieldDescriptor{"jumpBufferSeconds", "Jump Buffer Time (s)"}, jumpBufferSeconds_);
        builder.Float(AuthoringFieldDescriptor{"groundGraceSeconds", "Ground Grace Time (s)"}, groundGraceSeconds_);
        builder.Float(AuthoringFieldDescriptor{"maximumFallSpeed", "Maximum Fall Speed"}, maximumFallSpeed_);
    }
    if (builder.Section(TEXT::Localize("character.traversal"))) {
        builder.FloatRange(AuthoringFieldDescriptor{"maximumSlopeAngleDegrees", "Maximum Slope (deg)"},
                           maximumSlopeAngleDegrees_, 0.0f, 89.0f, 1.0f);
        builder.Float(AuthoringFieldDescriptor{"stepHeight", "Step Height"}, stepHeight_);
        builder.Float(AuthoringFieldDescriptor{"stickToFloorDistance", "Floor Snap Distance"}, stickToFloorDistance_);
        builder.Float(AuthoringFieldDescriptor{"stepForwardTestDistance", "Step Forward Test"},
                      stepForwardTestDistance_);
        builder.Bool(
            AuthoringFieldDescriptor{"cancelVelocityTowardsSteepSlopes", "Cancel Uphill Motion On Steep Slopes"},
            cancelVelocityTowardsSteepSlopes_);
        builder.Bool(AuthoringFieldDescriptor{"edgeGuardEnabled", "Prevent Grounded Edge Fall"}, edgeGuardEnabled_);
        builder.Float(AuthoringFieldDescriptor{"edgeProbeDistance", "Edge Probe Distance"}, edgeProbeDistance_);
        builder.Float(AuthoringFieldDescriptor{"edgeProbeRadius", "Edge Probe Radius"}, edgeProbeRadius_);
        builder.Float(AuthoringFieldDescriptor{"maximumGroundedDrop", "Maximum Grounded Drop"}, maximumGroundedDrop_);
    }
    if (builder.Section(TEXT::Localize("character.advanced"))) {
        builder.Float(AuthoringFieldDescriptor{"characterPadding", "Contact Padding"}, characterPadding_);
        builder.Float(AuthoringFieldDescriptor{"predictiveContactDistance", "Predictive Contact Distance"},
                      predictiveContactDistance_);
        builder.FloatRange(AuthoringFieldDescriptor{"penetrationRecoverySpeed", "Penetration Recovery"},
                           penetrationRecoverySpeed_, 0.0f, 1.0f, 0.05f);
        int maximumHits = static_cast<int>(maximumCollisionHits_);
        if (builder.Int(AuthoringFieldDescriptor{"maximumCollisionHits", "Maximum Contact Hits"}, maximumHits)) {
            maximumCollisionHits_ = static_cast<uint32_t>((std::clamp)(maximumHits, 16, 4096));
        }
        builder.Bool(AuthoringFieldDescriptor{"enhancedInternalEdgeRemoval", "Smooth Mesh Internal Edges"},
                     enhancedInternalEdgeRemoval_);
        builder.FloatRange(AuthoringFieldDescriptor{"inputDeadZone", "Input Dead Zone"}, inputDeadZone_, 0.0f, 0.95f);
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
    builder.String(AuthoringFieldDescriptor{"spawnPointId", "Spawn Point ID"}, spawnPointId_);
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
}

void DialogueInteractorComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    builder.Text("This actor can activate Dialogue Trigger volumes.");
}

void DialogueTriggerComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);
    builder.AssetIdPicker(AuthoringFieldDescriptor{"dialogueAssetGuid", "Dialogue Asset"}, AssetType::Dialogue,
                          dialogueAssetGuid_.value);
    builder.String(AuthoringFieldDescriptor{"locale", "Locale"}, locale_);
    static constexpr std::array<std::string_view, std::size(kDialogueTriggerActivationNames)> activationLabels{
        "On Enter", "Interact"};
    (void)EnumChoice(builder, AuthoringFieldDescriptor{"activation", "Activation"}, activation_,
                     kDialogueTriggerActivationNames, &activationLabels);
    if (activation_ == DialogueTriggerActivation::Interact) {
        builder.InputActionIdPicker(AuthoringFieldDescriptor{"interactAction", "Interact Action"},
                                    INPUT::InputActionValueType::Button, interactActionId_);
    }
    builder.Bool(AuthoringFieldDescriptor{"pauseWorld", "Pause World During Dialogue"}, pauseWorld_);
    builder.Bool(AuthoringFieldDescriptor{"oneShot", "One Shot"}, oneShot_);
    builder.Text(
        "Edit the paired Collider size and center to author the trigger range. The collider must remain a Trigger.");
    if (!dialogueAssetGuid_.IsValid()) {
        builder.Text("Assign a Dialogue asset before play.");
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
