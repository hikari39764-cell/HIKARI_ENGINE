#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class DialogueRuntimeSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "DialogueRuntimeSystem";
        }

        void OnWorldDetached(World& world) override;
        void PreUpdate(World& world, const FrameContext& frame) override;
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void LateUpdate(World& world, const FrameContext& frame) override;

    private:
        struct Candidate {
            RuntimeObjectHandle trigger{};
            RuntimeObjectHandle interactor{};
            // OnEnter contact can arrive while another dialogue owns the
            // session. Retain exactly one pending admission until the player
            // leaves or the session becomes available.
            bool pendingOnEnter = false;
        };

        bool TryAddCandidate(
            World& world,
            RuntimeObjectHandle possibleTrigger,
            RuntimeObjectHandle possibleInteractor,
            bool activateOnEnter);
        void RemoveCandidate(
            RuntimeObjectHandle possibleTrigger,
            RuntimeObjectHandle possibleInteractor);
        void PruneCandidates(World& world);
        bool TryStart(
            World& world,
            Candidate& candidate);

        std::vector<Candidate> candidates_{};
    };

} // namespace HIKARI
