#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::ANIMATION {

// Root motion is a delivery service, not an implicit GameObject write.
// AnimationSystem publishes after Update (order 150), targeting the next
// fixed tick; gameplay/SDK consumers consume exactly once for that
// (object, fixedTickIndex) pair.
class AnimationRootMotionService {
public:
    bool Publish(RuntimeObjectHandle object, uint64_t fixedTickIndex, const AnimationRootMotionDelta& delta) {
        if (!object.IsValid() || !delta.valid) {
            return false;
        }
        ObjectState& state = states_[object.ToValue()];
        if (fixedTickIndex < state.lastConsumedTick ||
            (fixedTickIndex == state.lastConsumedTick && state.hasConsumedTick)) {
            return false;
        }

        PendingDelta& pending = state.pending[fixedTickIndex];
        if (!pending.delta.valid) {
            pending.delta = delta;
            pending.delta.valid = true;
        } else {
            pending.delta.translation = pending.delta.translation +
                                        MATH::RotateVector(MATH::NormalizeQ(pending.delta.rotation), delta.translation);
            pending.delta.rotation = MATH::MultiplyQ(pending.delta.rotation, delta.rotation);
            pending.delta.valid = true;
        }
        pending.revision = ++state.revision;
        pending.delta.fixedTickIndex = fixedTickIndex;
        pending.delta.revision = pending.revision;
        return true;
    }

    bool Publish(RuntimeObjectHandle object, const AnimationRootMotionDelta& delta, uint64_t fixedTickIndex) {
        return Publish(object, fixedTickIndex, delta);
    }

    bool Consume(RuntimeObjectHandle object, uint64_t fixedTickIndex, AnimationRootMotionDelta& outDelta) noexcept {
        outDelta = {};
        if (!object.IsValid()) {
            return false;
        }
        const auto objectIt = states_.find(object.ToValue());
        if (objectIt == states_.end()) {
            return false;
        }
        ObjectState& state = objectIt->second;
        if (state.hasConsumedTick && fixedTickIndex <= state.lastConsumedTick) {
            return false;
        }
        const auto pendingIt = state.pending.find(fixedTickIndex);
        if (pendingIt == state.pending.end()) {
            for (auto stale = state.pending.begin(); stale != state.pending.end() && stale->first < fixedTickIndex;) {
                stale = state.pending.erase(stale);
            }
            state.lastConsumedTick = fixedTickIndex;
            state.hasConsumedTick = true;
            return false;
        }
        outDelta = pendingIt->second.delta;
        outDelta.fixedTickIndex = fixedTickIndex;
        outDelta.revision = pendingIt->second.revision;
        outDelta.valid = true;
        state.pending.erase(pendingIt);
        for (auto stale = state.pending.begin(); stale != state.pending.end() && stale->first <= fixedTickIndex;) {
            stale = state.pending.erase(stale);
        }
        state.lastConsumedTick = fixedTickIndex;
        state.hasConsumedTick = true;
        return true;
    }

    bool ConsumeRootMotion(RuntimeObjectHandle object, uint64_t fixedTickIndex,
                           AnimationRootMotionDelta& outDelta) noexcept {
        return Consume(object, fixedTickIndex, outDelta);
    }

    bool Peek(RuntimeObjectHandle object, uint64_t fixedTickIndex, AnimationRootMotionDelta& outDelta) const noexcept {
        outDelta = {};
        if (!object.IsValid()) {
            return false;
        }
        const auto objectIt = states_.find(object.ToValue());
        if (objectIt == states_.end()) {
            return false;
        }
        const auto pendingIt = objectIt->second.pending.find(fixedTickIndex);
        if (pendingIt == objectIt->second.pending.end()) {
            return false;
        }
        outDelta = pendingIt->second.delta;
        return true;
    }

    bool Find(RuntimeObjectHandle object, uint64_t fixedTickIndex, AnimationRootMotionDelta& outDelta) const noexcept {
        return Peek(object, fixedTickIndex, outDelta);
    }

    uint64_t GetRevision(RuntimeObjectHandle object) const noexcept {
        const auto found = states_.find(object.ToValue());
        return object.IsValid() && found != states_.end() ? found->second.revision : 0u;
    }

    void Remove(RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) {
            states_.erase(object.ToValue());
        }
    }

    void Clear() noexcept { states_.clear(); }

private:
    struct PendingDelta {
        AnimationRootMotionDelta delta{};
        uint64_t revision = 0u;
    };

    struct ObjectState {
        std::map<uint64_t, PendingDelta> pending{};
        uint64_t revision = 0u;
        uint64_t lastConsumedTick = 0u;
        bool hasConsumedTick = false;
    };

    std::unordered_map<uint64_t, ObjectState> states_{};
};

} // namespace HIKARI::ANIMATION
