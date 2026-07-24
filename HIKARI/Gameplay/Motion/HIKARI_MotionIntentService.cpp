#include "Gameplay/Motion/HIKARI_MotionIntentService.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace HIKARI::GAMEPLAY {

    void MotionIntentService::SubmitIntent(
        RuntimeObjectHandle object,
        MotionIntentSourceId source,
        int priority,
        const MotionIntent& intent,
        uint64_t frameIndex) {
        if (!object.IsValid() || source == 0u) {
            return;
        }

        std::vector<IntentSlot>& slots = intents_[object.ToValue()];
        auto found = std::find_if(
            slots.begin(),
            slots.end(),
            [source](const IntentSlot& slot) {
                return slot.source == source;
            });
        if (found == slots.end()) {
            slots.push_back(IntentSlot{});
            found = std::prev(slots.end());
            found->source = source;
        }
        found->priority = priority;
        found->submittedFrame = frameIndex;
        found->intent = intent;
        found->jumpLatched = found->jumpLatched || intent.jumpPressed;

        slots.erase(
            std::remove_if(
                slots.begin(),
                slots.end(),
                [frameIndex](const IntentSlot& slot) {
                    return frameIndex > slot.submittedFrame + 2u &&
                        !slot.jumpLatched;
                }),
            slots.end());
    }

    bool MotionIntentService::ResolveIntent(
        RuntimeObjectHandle object,
        uint64_t frameIndex,
        MotionIntent& outIntent,
        MotionIntentSourceId* outSource) {
        outIntent = {};
        IntentSlot* selected = FindSelectedSlot(object, frameIndex);
        if (selected == nullptr) {
            return false;
        }

        outIntent = selected->intent;
        outIntent.jumpPressed = selected->jumpLatched;
        selected->jumpLatched = false;
        if (outSource != nullptr) {
            *outSource = selected->source;
        }
        return true;
    }

    bool MotionIntentService::PeekIntent(
        RuntimeObjectHandle object,
        uint64_t frameIndex,
        MotionIntent& outIntent,
        MotionIntentSourceId* outSource) const {
        outIntent = {};
        const IntentSlot* selected = FindSelectedSlot(object, frameIndex);
        if (selected == nullptr) {
            return false;
        }

        outIntent = selected->intent;
        outIntent.jumpPressed = selected->jumpLatched;
        if (outSource != nullptr) {
            *outSource = selected->source;
        }
        return true;
    }

    MotionIntentService::IntentSlot* MotionIntentService::FindSelectedSlot(
        RuntimeObjectHandle object,
        uint64_t frameIndex) noexcept {
        return const_cast<IntentSlot*>(
            std::as_const(*this).FindSelectedSlot(object, frameIndex));
    }

    const MotionIntentService::IntentSlot*
    MotionIntentService::FindSelectedSlot(
        RuntimeObjectHandle object,
        uint64_t frameIndex) const noexcept {
        if (!object.IsValid()) {
            return nullptr;
        }
        const auto actor = intents_.find(object.ToValue());
        if (actor == intents_.end()) {
            return nullptr;
        }

        const IntentSlot* selected = nullptr;
        for (const IntentSlot& slot : actor->second) {
            if (slot.submittedFrame != frameIndex) {
                continue;
            }
            if (selected == nullptr || slot.priority > selected->priority ||
                (slot.priority == selected->priority &&
                    slot.source < selected->source)) {
                selected = &slot;
            }
        }
        return selected;
    }

    void MotionIntentService::Remove(
        RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) {
            intents_.erase(object.ToValue());
        }
    }

    void MotionIntentService::Clear() noexcept {
        intents_.clear();
    }

} // namespace HIKARI::GAMEPLAY
