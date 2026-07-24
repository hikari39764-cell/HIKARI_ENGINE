#pragma once

#include <string_view>
#include <unordered_map>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::ANIMATION {

    class AnimationStateMachineRuntimeService {
    public:
        AnimationStateMachineInstance& Acquire(RuntimeObjectHandle object);
        AnimationStateMachineInstance* Find(
            RuntimeObjectHandle object) noexcept;
        const AnimationStateMachineInstance* Find(
            RuntimeObjectHandle object) const noexcept;
        void Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

        bool Start(RuntimeObjectHandle object, bool restart = true);
        bool Stop(RuntimeObjectHandle object) noexcept;

        bool SetBool(
            RuntimeObjectHandle object,
            std::string_view parameter,
            bool value);
        bool SetFloat(
            RuntimeObjectHandle object,
            std::string_view parameter,
            float value);
        bool SetInteger(
            RuntimeObjectHandle object,
            std::string_view parameter,
            int32_t value);
        bool FireTrigger(
            RuntimeObjectHandle object,
            std::string_view parameter);

    private:
        std::unordered_map<uint64_t, AnimationStateMachineInstance>
            instances_{};
    };

} // namespace HIKARI::ANIMATION
