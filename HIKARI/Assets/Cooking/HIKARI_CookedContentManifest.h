#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetRecord.h"

namespace HIKARI::ASSETS::COOKING {

inline constexpr char kCookedContentManifestFilename[] = "CookedAssetRegistry.json";
inline constexpr uint32_t kCookedContentManifestFormatVersion = 1u;

struct CookedContentManifest {
    uint32_t formatVersion = kCookedContentManifestFormatVersion;
    std::string startupSceneGuid{};
    std::vector<std::string> exportedSceneGuids{};
    std::vector<AssetRecord> assets{};
};

bool ReadCookedContentManifest(const std::filesystem::path& path, CookedContentManifest& outManifest,
                               std::string* outMessage = nullptr);

bool WriteCookedContentManifest(const std::filesystem::path& path, const CookedContentManifest& manifest,
                                std::string* outMessage = nullptr);

} // namespace HIKARI::ASSETS::COOKING
