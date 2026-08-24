#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI {

    struct SequenceAsset {
        static constexpr uint32_t kFormatVersion = 1;

        AssetGuid guid{};
        std::string displayName{};
        CinematicSequence sequence{};
    };

    bool LoadSequenceAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        SequenceAsset& outAsset,
        std::string* outError = nullptr);
    bool SaveSequenceAsset(
        const std::filesystem::path& path,
        const SequenceAsset& asset,
        std::string* outError = nullptr);

} // namespace HIKARI
