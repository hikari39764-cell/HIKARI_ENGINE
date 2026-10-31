// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"

#include <string>
#include <string_view>
#include <utility>

#include "Authoring/HIKARI_FieldInspector.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

namespace {
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
    builder.Text(TEXT::Localize("character.inputHint"));
    CharacterInputSettings next = settings_;
    bool changed = InspectFields(builder, next, kCharacterInputFields);
    if (builder.Button("Use Gameplay Defaults")) {
        const CharacterInputSettings defaults{};
        next.moveAction = defaults.moveAction;
        next.lookAction = defaults.lookAction;
        next.jumpAction = defaults.jumpAction;
        next.sprintAction = defaults.sprintAction;
        changed = true;
    }
    if (changed) {
        settings_ = std::move(next);
    }

    const InspectorContext& context = builder.GetContext();
    const auto* inputService =
        context.worldServices != nullptr ? context.worldServices->Find<INPUT::InputService>() : nullptr;
    if (inputService != nullptr) {
        const INPUT::InputActionMap& actionMap = inputService->GetActionMap();
        DrawActionValidation(builder, actionMap, "Move", settings_.moveAction, INPUT::InputActionValueType::Axis2D);
        DrawActionValidation(builder, actionMap, "Look", settings_.lookAction, INPUT::InputActionValueType::Axis2D);
        DrawActionValidation(builder, actionMap, "Jump", settings_.jumpAction, INPUT::InputActionValueType::Button);
        DrawActionValidation(builder, actionMap, "Sprint", settings_.sprintAction, INPUT::InputActionValueType::Button);
    }
}

void CharacterLocomotionComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Text(TEXT::Localize("character.locomotionHint"));
    CharacterLocomotionSettings next = settings_;
    bool changed = InspectFields(builder, next, kCharacterLocomotionFields);
    if (builder.Button("Use Third-Person Movement")) {
        next.movementSpace = CharacterMovementSpace::Camera;
        next.rotateToMove = true;
        next.edgeGuardEnabled = true;
        changed = true;
    }
    if (changed) {
        settings_ = NormalizeCharacterLocomotionSettings(std::move(next));
    }
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
    SpawnPointSettings next = settings_;
    if (InspectFields(builder, next, kSpawnPointFields)) {
        settings_ = std::move(next);
    }
}

void DialogueInteractorComponent::BuildInspector(IInspectorBuilder& builder) {
    DialogueInteractorSettings next = settings_;
    if (InspectFields(builder, next, kDialogueInteractorFields)) {
        settings_ = next;
    }
}

void DialogueTriggerComponent::BuildInspector(IInspectorBuilder& builder) {
    DialogueTriggerSettings next = settings_;
    if (InspectFields(builder, next, kDialogueTriggerFields)) {
        settings_ = std::move(next);
    }
    builder.Text(
        "Edit the paired Collider size and center to author the trigger range. The collider must remain a Trigger.");
    if (!settings_.dialogueAssetGuid.IsValid()) {
        builder.Text("Assign a Dialogue asset before play.");
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
