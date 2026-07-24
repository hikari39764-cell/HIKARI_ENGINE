#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Gameplay/Motion/HIKARI_MotionIntentTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::GAMEPLAY {

    class MotionIntentService {
    public:
        void SubmitIntent(
            RuntimeObjectHandle object,
            MotionIntentSourceId source,
            int priority,
            const MotionIntent& intent,
            uint64_t frameIndex);
        bool ResolveIntent(
            RuntimeObjectHandle object,
            uint64_t frameIndex,
            MotionIntent& outIntent,
            MotionIntentSourceId* outSource = nullptr);
        bool PeekIntent(
            RuntimeObjectHandle object,
            uint64_t frameIndex,
            MotionIntent& outIntent,
            MotionIntentSourceId* outSource = nullptr) const;

        void Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

    private:
        struct IntentSlot {
            MotionIntentSourceId source = 0u;
            int priority = 0;
            uint64_t submittedFrame = 0u;
            MotionIntent intent{};
            bool jumpLatched = false;
        };

        IntentSlot* FindSelectedSlot(
            RuntimeObjectHandle object,
            uint64_t frameIndex) noexcept;
        const IntentSlot* FindSelectedSlot(
            RuntimeObjectHandle object,
            uint64_t frameIndex) const noexcept;

        std::unordered_map<uint64_t, std::vector<IntentSlot>> intents_{};
    };

} // namespace HIKARI::GAMEPLAY
