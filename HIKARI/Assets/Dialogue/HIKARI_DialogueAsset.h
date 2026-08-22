#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"

namespace HIKARI {

    struct DialogueAsset {
        static constexpr uint32_t kMinimumReadableFormatVersion = 1u;
        static constexpr uint32_t kFormatVersion = 3u;

        AssetGuid guid{};
        std::string displayName{};
        DIALOGUE::DialogueDefinition definition{};
    };

    bool LoadDialogueAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        DialogueAsset& outAsset,
        std::string* outError = nullptr);
    bool SaveDialogueAsset(
        const std::filesystem::path& path,
        const DialogueAsset& asset,
        std::string* outError = nullptr);

} // namespace HIKARI
