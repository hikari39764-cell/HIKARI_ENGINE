#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"

namespace HIKARI {

inline constexpr uint32_t kAssetArtifactManifestVersion = 2u;

struct AssetImportInputFile {
    std::string path;
    std::string digest;
};

struct AssetImportInputs {
    std::string settings;
    std::vector<AssetImportInputFile> files;
};

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
    AssetImportInputs inputs{};

    bool lastImportSucceeded = false;
    std::string lastImportMessage{};
    std::string diagnosticsJson{};

    std::vector<AssetDependencyDesc> dependencies{};
    std::vector<AssetArtifactDesc> artifacts{};
};

} // namespace HIKARI
