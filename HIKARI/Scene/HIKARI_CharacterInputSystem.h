#pragma once

#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
    struct RuntimePlayStateService;
    namespace GAMEPLAY { class MotionIntentService; }
    namespace INPUT { class InputService; }

    class CharacterInputSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return "CharacterInputSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        INPUT::InputService* inputService_ = nullptr;
        GAMEPLAY::MotionIntentService* motionIntentService_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
    };

} // namespace HIKARI
