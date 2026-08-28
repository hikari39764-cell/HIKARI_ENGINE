#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "HIKARI_AssetGuid.h"

namespace HIKARI {

    inline constexpr uint32_t kAssetArtifactManifestVersion = 1u;

    struct AssetDependencyDesc {
        AssetGuid guid{};
        std::string path{};
        std::string role{};
    };

    struct AssetArtifactDesc {
        std::string role{};
        std::string path{};
        std::string format{};
    };

    struct AssetArtifactManifest {
        uint32_t manifestVersion = kAssetArtifactManifestVersion;

        AssetGuid guid{};
        std::string sourcePath{};
        std::string importerId{};
        uint32_t importerVersion = 1;

        bool lastImportSucceeded = false;
        std::string lastImportMessage{};
        std::string diagnosticsJson{};

        std::vector<AssetDependencyDesc> dependencies{};
        std::vector<AssetArtifactDesc> artifacts{};
    };

} // namespace HIKARI
