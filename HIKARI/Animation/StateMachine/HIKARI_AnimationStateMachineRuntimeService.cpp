#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"

namespace HIKARI::ANIMATION {

    AnimationStateMachineInstance&
        AnimationStateMachineRuntimeService::Acquire(
            RuntimeObjectHandle object) {
        return instances_[object.ToValue()];
    }

    AnimationStateMachineInstance*
        AnimationStateMachineRuntimeService::Find(
            RuntimeObjectHandle object) noexcept {
        const auto found = instances_.find(object.ToValue());
        return found != instances_.end() ? &found->second : nullptr;
    }

    const AnimationStateMachineInstance*
        AnimationStateMachineRuntimeService::Find(
            RuntimeObjectHandle object) const noexcept {
        const auto found = instances_.find(object.ToValue());
        return found != instances_.end() ? &found->second : nullptr;
    }

    void AnimationStateMachineRuntimeService::Remove(
        RuntimeObjectHandle object) noexcept {
        instances_.erase(object.ToValue());
    }

    void AnimationStateMachineRuntimeService::Clear() noexcept {
        instances_.clear();
    }

    bool AnimationStateMachineRuntimeService::Start(
        RuntimeObjectHandle object,
        bool restart) {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->Start(restart);
    }

    bool AnimationStateMachineRuntimeService::Stop(
        RuntimeObjectHandle object) noexcept {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->Stop();
    }

    bool AnimationStateMachineRuntimeService::SetBool(
        RuntimeObjectHandle object,
        std::string_view parameter,
        bool value) {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->SetBool(parameter, value);
    }

    bool AnimationStateMachineRuntimeService::SetFloat(
        RuntimeObjectHandle object,
        std::string_view parameter,
        float value) {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->SetFloat(parameter, value);
    }

    bool AnimationStateMachineRuntimeService::SetInteger(
        RuntimeObjectHandle object,
        std::string_view parameter,
        int32_t value) {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->SetInteger(parameter, value);
    }

    bool AnimationStateMachineRuntimeService::FireTrigger(
        RuntimeObjectHandle object,
        std::string_view parameter) {
        AnimationStateMachineInstance* instance = Find(object);
        return instance != nullptr && instance->FireTrigger(parameter);
    }

} // namespace HIKARI::ANIMATION
