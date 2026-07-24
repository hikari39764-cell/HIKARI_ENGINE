#include "Scene/Components/HIKARI_CharacterInputComponent.h"

#include <string>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {
    namespace {
        void DrawActionValidation(
            IInspectorBuilder& builder,
            const INPUT::InputActionMap& actionMap,
            std::string_view label,
            const std::string& actionId,
            INPUT::InputActionValueType expectedType) {
            const INPUT::InputActionDefinition* action =
                actionMap.FindAction(actionId);
            if (action == nullptr) {
                builder.Text(
                    std::string(label) + " action is missing: " +
                    actionId);
            } else if (action->valueType != expectedType) {
                builder.Text(
                    std::string(label) + " must use " +
                    INPUT::ToString(expectedType) + ", but project action is " +
                    INPUT::ToString(action->valueType) + ".");
            }
        }
    }

    void CharacterInputComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["priority"] = priority_;
        out["moveAction"] = moveActionId_;
        out["lookAction"] = lookActionId_;
        out["jumpAction"] = jumpActionId_;
        out["sprintAction"] = sprintActionId_;
    }

    void CharacterInputComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        priority_ = in.value("priority", priority_);
        moveActionId_ = in.value("moveAction", moveActionId_);
        lookActionId_ = in.value("lookAction", lookActionId_);
        jumpActionId_ = in.value("jumpAction", jumpActionId_);
        sprintActionId_ = in.value("sprintAction", sprintActionId_);
    }

    void CharacterInputComponent::BuildInspector(
        IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Int("Control Priority", priority_);
        if (builder.Button("Use Gameplay Defaults")) {
            moveActionId_ = "Gameplay.Move";
            lookActionId_ = "Gameplay.Look";
            jumpActionId_ = "Gameplay.Jump";
            sprintActionId_ = "Gameplay.Sprint";
        }
        builder.InputActionIdPicker(
            "Move Action",
            INPUT::InputActionValueType::Axis2D,
            moveActionId_);
        builder.InputActionIdPicker(
            "Look Action",
            INPUT::InputActionValueType::Axis2D,
            lookActionId_);
        builder.InputActionIdPicker(
            "Jump Action",
            INPUT::InputActionValueType::Button,
            jumpActionId_);
        builder.InputActionIdPicker(
            "Sprint Action",
            INPUT::InputActionValueType::Button,
            sprintActionId_);

        const InspectorContext& context = builder.GetContext();
        const auto* inputService = context.worldServices != nullptr
            ? context.worldServices->Find<INPUT::InputService>()
            : nullptr;
        if (inputService != nullptr) {
            const INPUT::InputActionMap& actionMap =
                inputService->GetActionMap();
            DrawActionValidation(
                builder,
                actionMap,
                "Move",
                moveActionId_,
                INPUT::InputActionValueType::Axis2D);
            DrawActionValidation(
                builder,
                actionMap,
                "Look",
                lookActionId_,
                INPUT::InputActionValueType::Axis2D);
            DrawActionValidation(
                builder,
                actionMap,
                "Jump",
                jumpActionId_,
                INPUT::InputActionValueType::Button);
            DrawActionValidation(
                builder,
                actionMap,
                "Sprint",
                sprintActionId_,
                INPUT::InputActionValueType::Button);
        }
    }

} // namespace HIKARI
