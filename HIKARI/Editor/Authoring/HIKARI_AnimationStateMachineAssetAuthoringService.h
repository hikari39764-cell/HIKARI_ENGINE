#pragma once

#include <filesystem>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    struct AnimationStateMachineAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    AnimationStateMachineAssetAuthoringResult
        CreateAnimationStateMachineAsset(
            AssetDatabase& assetDatabase,
            const ANIMATION::AnimationStateMachineDefinition& definition);
    AnimationStateMachineAssetAuthoringResult
        UpdateAnimationStateMachineAsset(
            AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            const ANIMATION::AnimationStateMachineDefinition& definition);
    bool LoadAnimationStateMachineForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        ANIMATION::AnimationStateMachineDefinition& outDefinition,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
