#include "Scene/HIKARI_CharacterInputSystem.h"

#include <array>

#include "Core/HIKARI_FrameContext.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/Components/HIKARI_CharacterInputComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    void CharacterInputSystem::OnWorldAttached(World& world) {
        inputService_ = world.Services().Find<INPUT::InputService>();
        motionIntentService_ = world.Services().Find<
            GAMEPLAY::MotionIntentService>();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
    }

    void CharacterInputSystem::OnWorldDetached(World&) {
        inputService_ = nullptr;
        motionIntentService_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void CharacterInputSystem::PreUpdate(
        World& world,
        const FrameContext& frame) {
        if (inputService_ == nullptr || motionIntentService_ == nullptr) {
            return;
        }
#if defined(HIKARI_WITH_EDITOR)
        if (runtimePlayState_ == nullptr ||
            !runtimePlayState_->IsActive()) {
            return;
        }
#endif

        const INPUT::InputSnapshot& snapshot =
            inputService_->GetSnapshot();
        world.ForEachObjectWith<CharacterInputComponent>(
            [this, &snapshot, &frame](
                GameObject& object,
                CharacterInputComponent& input) {
                if (!input.IsEnabled()) {
                    return;
                }
                const std::array<float, 2> move = snapshot.GetAxis2D(
                    input.GetMoveActionId());
                const std::array<float, 2> look = snapshot.GetAxis2D(
                    input.GetLookActionId());
                GAMEPLAY::MotionIntent intent{};
                intent.move = { move[0], move[1] };
                intent.look = { look[0], look[1] };
                intent.jumpPressed = snapshot.IsPressed(
                    input.GetJumpActionId());
                intent.jumpHeld = snapshot.IsDown(
                    input.GetJumpActionId());
                intent.sprintHeld = snapshot.IsDown(
                    input.GetSprintActionId());
                motionIntentService_->SubmitIntent(
                    object.GetRuntimeHandle(),
                    GAMEPLAY::kPlayerInputMotionSource,
                    input.GetPriority(),
                    intent,
                    frame.frameIndex);
            });
    }

} // namespace HIKARI
