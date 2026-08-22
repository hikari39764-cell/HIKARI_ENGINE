#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    struct DialogueAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    DialogueAssetAuthoringResult CreateDialogueAsset(
        AssetDatabase& assetDatabase,
        const DIALOGUE::DialogueDefinition& definition);
    DialogueAssetAuthoringResult UpdateDialogueAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        const DIALOGUE::DialogueDefinition& definition);
    bool LoadDialogueForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        DIALOGUE::DialogueDefinition& outDefinition,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
