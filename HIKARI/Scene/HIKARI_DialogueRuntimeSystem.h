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
            const Candidate& candidate);

        std::vector<Candidate> candidates_{};
    };

} // namespace HIKARI
