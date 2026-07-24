#include "Gameplay/Motion/HIKARI_CharacterMotionStateService.h"

namespace HIKARI::GAMEPLAY {

    void CharacterMotionStateService::Publish(
        const CharacterMotionState& state) {
        if (state.object.IsValid()) {
            states_[state.object.ToValue()] = state;
        }
    }

    const CharacterMotionState* CharacterMotionStateService::Find(
        RuntimeObjectHandle object) const noexcept {
        const auto found = states_.find(object.ToValue());
        return object.IsValid() && found != states_.end()
            ? &found->second
            : nullptr;
    }

    void CharacterMotionStateService::Remove(
        RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) states_.erase(object.ToValue());
    }

    void CharacterMotionStateService::Clear() noexcept {
        states_.clear();
    }

} // namespace HIKARI::GAMEPLAY
