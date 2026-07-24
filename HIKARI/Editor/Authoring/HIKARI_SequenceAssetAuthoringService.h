#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    struct SequenceAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    SequenceAssetAuthoringResult CreateSequenceAsset(
        AssetDatabase& assetDatabase,
        const CinematicSequence& sequence);
    SequenceAssetAuthoringResult UpdateSequenceAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        const CinematicSequence& sequence);
    bool LoadSequenceForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        CinematicSequence& outSequence,
        std::string& outMessage);

} // namespace EDITOR
} // namespace HIKARI
