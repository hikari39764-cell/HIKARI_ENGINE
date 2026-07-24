#pragma once

#include <unordered_map>

#include "Gameplay/Motion/HIKARI_CharacterMotionState.h"

namespace HIKARI::GAMEPLAY {

    class CharacterMotionStateService {
    public:
        void Publish(const CharacterMotionState& state);
        const CharacterMotionState* Find(
            RuntimeObjectHandle object) const noexcept;
        void Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

    private:
        std::unordered_map<uint64_t, CharacterMotionState> states_{};
    };

} // namespace HIKARI::GAMEPLAY
