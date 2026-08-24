#pragma once

#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"

namespace HIKARI {

    class GameObject;
    class SequencePlayerComponent;
    struct RuntimePlayStateService;

namespace SEQUENCER {
    class SequencePlaybackService;
}

    class SequencePlayerSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "SequencePlayerSystem";
        }
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;

    private:
        void ProcessComponent(
            GameObject& owner,
            SequencePlayerComponent& component);
        void PlayComponent(
            GameObject& owner,
            SequencePlayerComponent& component);
        void StopComponent(
            SequencePlayerComponent& component,
            SEQUENCER::SequenceStopReason reason);

        SEQUENCER::SequencePlaybackService* playbackService_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        std::vector<SEQUENCER::SequencePlaybackHandle> trackedHandles_{};
    };

} // namespace HIKARI
