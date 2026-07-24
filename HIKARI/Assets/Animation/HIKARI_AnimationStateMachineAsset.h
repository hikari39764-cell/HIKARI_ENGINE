#pragma once

#include <filesystem>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    struct AnimationStateMachineAsset {
        static constexpr uint32_t kFormatVersion = 2u;

        AssetGuid guid{};
        std::string displayName{};
        ANIMATION::AnimationStateMachineDefinition definition{};
    };

    bool LoadAnimationStateMachineAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        AnimationStateMachineAsset& outAsset,
        std::string* outError = nullptr);
    bool SaveAnimationStateMachineAsset(
        const std::filesystem::path& path,
        const AnimationStateMachineAsset& asset,
        std::string* outError = nullptr);

} // namespace HIKARI
