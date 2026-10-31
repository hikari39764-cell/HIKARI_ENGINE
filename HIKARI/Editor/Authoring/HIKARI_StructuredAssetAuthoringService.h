#pragma once
#include <filesystem>
#include <string>
#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"
#include "Core/HIKARI_AssetGuid.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {
class AssetDatabase;
}
namespace HIKARI::EDITOR {
struct StructuredAssetAuthoringResult {
    bool success = false;
    AssetGuid assetGuid;
    std::filesystem::path sourcePath;
    std::string message;
};
StructuredAssetAuthoringResult CreateAnimationStateMachineAsset(
    AssetDatabase& database, const ANIMATION::AnimationStateMachineDefinition& definition);
StructuredAssetAuthoringResult UpdateAnimationStateMachineAsset(
    AssetDatabase& database, const AssetGuid& guid, const ANIMATION::AnimationStateMachineDefinition& definition);
bool LoadAnimationStateMachineForAuthoring(const AssetDatabase& database, const AssetGuid& guid,
                                           ANIMATION::AnimationStateMachineDefinition& definition,
                                           std::string& message);
StructuredAssetAuthoringResult CreateDialogueAsset(AssetDatabase& database,
                                                   const DIALOGUE::DialogueDefinition& definition);
StructuredAssetAuthoringResult UpdateDialogueAsset(AssetDatabase& database, const AssetGuid& guid,
                                                   const DIALOGUE::DialogueDefinition& definition);
bool LoadDialogueForAuthoring(const AssetDatabase& database, const AssetGuid& guid,
                              DIALOGUE::DialogueDefinition& definition, std::string& message);
StructuredAssetAuthoringResult CreateSequenceAsset(AssetDatabase& database, const CinematicSequence& definition);
StructuredAssetAuthoringResult UpdateSequenceAsset(AssetDatabase& database, const AssetGuid& guid,
                                                   const CinematicSequence& definition);
bool LoadSequenceForAuthoring(const AssetDatabase& database, const AssetGuid& guid, CinematicSequence& definition,
                              std::string& message);
} // namespace HIKARI::EDITOR
