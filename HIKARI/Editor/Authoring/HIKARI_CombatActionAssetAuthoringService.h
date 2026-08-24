#pragma once

#include <filesystem>
#include <string>

#include "Assets/Action/HIKARI_CombatActionAsset.h"

namespace HIKARI { class AssetDatabase; }

namespace HIKARI::EDITOR {

    struct CombatActionAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    CombatActionAssetAuthoringResult CreateCombatActionAsset(
        AssetDatabase& assetDatabase,
        const ACTION::CombatActionDefinition& definition);
    CombatActionAssetAuthoringResult UpdateCombatActionAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        const ACTION::CombatActionDefinition& definition);
    bool LoadCombatActionForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        ACTION::CombatActionDefinition& outDefinition,
        std::string& outMessage);

} // namespace HIKARI::EDITOR
