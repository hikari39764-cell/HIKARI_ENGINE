#pragma once

#include <cstdint>
#include <string>

#include "HIKARI_AssetGuid.h"
#include "HIKARI_AssetTypes.h"

namespace HIKARI {

    struct AssetMeta {
        uint32_t metaVersion = 1;

        AssetGuid guid{};
        AssetType type = AssetType::Unknown;

        std::string importerId{};
        uint32_t importerVersion = 1;

        std::string sourcePath{};
        std::string displayName{};

        std::string importSettingsJson{};
    };

} // namespace HIKARI
