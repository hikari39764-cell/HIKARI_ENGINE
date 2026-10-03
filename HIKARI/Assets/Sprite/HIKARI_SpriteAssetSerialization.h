#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Sprite/HIKARI_SpriteAsset.h"

namespace HIKARI::SPRITE {

inline constexpr std::string_view kRuntimeAssetFormat = "hikari.sprite.runtime";
inline constexpr uint32_t kRuntimeAssetVersion = 1u;

struct SpriteSourceDocument {
    SpriteAssetData asset;
    std::vector<std::filesystem::path> pagePaths;
    std::vector<std::filesystem::path> normalPagePaths;
};

bool LoadSpriteSourceDocument(const std::filesystem::path& path, std::string_view sourceGuid,
                              std::string_view fallbackName, SpriteSourceDocument& outDocument, std::string& outError);
bool ValidateSpriteRuntimeAsset(const SpriteAssetData& asset, std::string_view sourceGuid, std::string& outError,
                                bool requirePageArtifacts = true);
bool WriteSpriteRuntimeAsset(const std::filesystem::path& path, std::string_view sourceGuid,
                             const SpriteAssetData& asset, std::string& outError);
bool LoadSpriteAssetData(const std::filesystem::path& path, SpriteAssetData& outAsset, std::string& outError);

} // namespace HIKARI::SPRITE
