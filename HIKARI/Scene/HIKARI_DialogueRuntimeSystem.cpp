#include "Scene/HIKARI_DialogueRuntimeSystem.h"

#include <algorithm>

#include "Core/HIKARI_FrameContext.h"
#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Components/HIKARI_DialogueInteractorComponent.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    void DialogueRuntimeSystem::OnWorldDetached(World& world) {
        if (auto* session = world.Services().Find<
                DIALOGUE::DialogueSessionService>()) {
            session->Reset(&world);
        }
        candidates_.clear();
    }

    bool DialogueRuntimeSystem::TryAddCandidate(
        World& world,
        RuntimeObjectHandle possibleTrigger,
        RuntimeObjectHandle possibleInteractor,
        bool activateOnEnter) {
        GameObject* triggerObject = world.FindObject(possibleTrigger);
        GameObject* interactorObject = world.FindObject(possibleInteractor);
        if (triggerObject == nullptr || interactorObject == nullptr) {
            return false;
        }
        DialogueTriggerComponent* trigger = triggerObject->GetComponent<
            DialogueTriggerComponent>();
        DialogueInteractorComponent* interactor =
            interactorObject->GetComponent<DialogueInteractorComponent>();
        if (trigger == nullptr || interactor == nullptr ||
            !trigger->IsEnabled() || !interactor->IsEnabled() ||
            !trigger->GetDialogueAssetGuid().IsValid() ||
            (trigger->IsOneShot() && trigger->IsConsumed())) {
            return false;
        }
        const Candidate candidate{ possibleTrigger, possibleInteractor };
        const bool exists = std::any_of(
            candidates_.begin(),
            candidates_.end(),
            [&](const Candidate& existing) {
                return existing.trigger == candidate.trigger &&
                    existing.interactor == candidate.interactor;
            });
        if (!exists) {
            candidates_.push_back(candidate);
            std::stable_sort(
                candidates_.begin(),
                candidates_.end(),
                [](const Candidate& lhs, const Candidate& rhs) {
                    return lhs.trigger.ToValue() < rhs.trigger.ToValue();
                });
        }
        if (activateOnEnter &&
            trigger->GetActivation() == DialogueTriggerActivation::OnEnter) {
            return TryStart(world, candidate);
        }
        return true;
    }

    void DialogueRuntimeSystem::RemoveCandidate(
        RuntimeObjectHandle possibleTrigger,
        RuntimeObjectHandle possibleInteractor) {
        std::erase_if(
            candidates_,
            [&](const Candidate& candidate) {
                return candidate.trigger == possibleTrigger &&
                    candidate.interactor == possibleInteractor;
            });
    }

    void DialogueRuntimeSystem::PruneCandidates(World& world) {
        std::erase_if(
            candidates_,
            [&](const Candidate& candidate) {
                GameObject* triggerObject = world.FindObject(
                    candidate.trigger);
                GameObject* interactorObject = world.FindObject(
                    candidate.interactor);
                const auto* trigger = triggerObject != nullptr
                    ? triggerObject->GetComponent<DialogueTriggerComponent>()
                    : nullptr;
                const auto* interactor = interactorObject != nullptr
                    ? interactorObject->GetComponent<
                        DialogueInteractorComponent>()
                    : nullptr;
                return trigger == nullptr || interactor == nullptr ||
                    !trigger->IsEnabled() || !interactor->IsEnabled() ||
                    (trigger->IsOneShot() && trigger->IsConsumed());
            });
    }

    bool DialogueRuntimeSystem::TryStart(
        World& world,
        const Candidate& candidate) {
        auto* session = world.Services().Find<
            DIALOGUE::DialogueSessionService>();
        if (session == nullptr || session->IsActive()) {
            return false;
        }
        GameObject* triggerObject = world.FindObject(candidate.trigger);
        DialogueTriggerComponent* trigger = triggerObject != nullptr
            ? triggerObject->GetComponent<DialogueTriggerComponent>()
            : nullptr;
        if (trigger == nullptr || !trigger->IsEnabled()) {
            return false;
        }
        if (!session->Start(
                world,
                trigger->GetDialogueAssetGuid(),
                trigger->GetLocale(),
                candidate.trigger,
                candidate.interactor,
                trigger->ShouldPauseWorld())) {
            return false;
        }
        if (trigger->IsOneShot()) trigger->MarkConsumed();
        return true;
    }

    void DialogueRuntimeSystem::PreUpdate(
        World& world,
        const FrameContext& frame) {
        auto* session = world.Services().Find<
            DIALOGUE::DialogueSessionService>();
        if (session == nullptr) return;
        session->Update(world, frame);
        if (session->IsActive()) return;
        PruneCandidates(world);
        const INPUT::InputSnapshot& input =
            SERVICES::GetInputService().GetSnapshot();
        for (const Candidate& candidate : candidates_) {
            GameObject* object = world.FindObject(candidate.trigger);
            const DialogueTriggerComponent* trigger = object != nullptr
                ? object->GetComponent<DialogueTriggerComponent>()
                : nullptr;
            if (trigger != nullptr &&
                trigger->GetActivation() ==
                    DialogueTriggerActivation::Interact &&
                input.IsPressed(trigger->GetInteractActionId()) &&
                TryStart(world, candidate)) {
                break;
            }
        }
    }

    void DialogueRuntimeSystem::PostFixedUpdate(
        World& world,
        const FrameContext&) {
        const auto events = world.FixedEvents().Read<
            PHYSICS::PhysicsContactEvent>();
        for (const PHYSICS::PhysicsContactEvent& event : events) {
            if (!event.isTrigger) continue;
            if (event.phase == PHYSICS::PhysicsContactPhase::End) {
                RemoveCandidate(event.objectA, event.objectB);
                RemoveCandidate(event.objectB, event.objectA);
                continue;
            }
            if (event.phase != PHYSICS::PhysicsContactPhase::Begin) {
                continue;
            }
            (void)TryAddCandidate(
                world,
                event.objectA,
                event.objectB,
                true);
            (void)TryAddCandidate(
                world,
                event.objectB,
                event.objectA,
                true);
        }
    }

    void DialogueRuntimeSystem::LateUpdate(
        World& world,
        const FrameContext& frame) {
        if (auto* session = world.Services().Find<
                DIALOGUE::DialogueSessionService>()) {
            session->SubmitPresentation(world, frame);
        }
    }

} // namespace HIKARI
