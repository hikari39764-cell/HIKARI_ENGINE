#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"
#include "Assets/Shader/HIKARI_ShaderParameters.h"

namespace HIKARI::SPRITE {

inline constexpr uint32_t kSpriteMaterialAssetVersion = 1u;
inline constexpr std::string_view kSpriteMaterialRuntimeFormat = "hikari.sprite-material.runtime";

struct SpriteMaterialAssetData {
    uint32_t version = kSpriteMaterialAssetVersion;
    AssetGuid sourceAssetGuid{};
    std::string name{};
    AssetGuid shaderAssetGuid{};
    std::vector<ASSETS::SHADER::ShaderParameterOverride> parameters{};
};

struct SpriteMaterialBinding {
    AssetGuid materialAssetGuid{};
    AssetGuid shaderAssetGuid{};
    std::shared_ptr<const SpriteMaterialAssetData> asset{};
    std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact> shaderArtifact{};
    ASSETS::SHADER::ShaderParameterSlots parameterSlots{};
};

bool ValidateSpriteMaterialAsset(const SpriteMaterialAssetData& asset,
                                 std::span<const ASSETS::SHADER::ShaderParameterDefinition> definitions,
                                 std::string& outError);
bool LoadSpriteMaterialAssetData(const std::filesystem::path& path, SpriteMaterialAssetData& outAsset,
                                 std::string& outError);
bool WriteSpriteMaterialAsset(const std::filesystem::path& path, const SpriteMaterialAssetData& asset,
                              std::string& outError);
bool WriteSpriteMaterialAssetArtifact(const std::filesystem::path& path, const AssetGuid& sourceGuid,
                                      const SpriteMaterialAssetData& asset, std::string& outError);
bool LoadSpriteMaterialAssetArtifact(const std::filesystem::path& path, const AssetGuid& expectedSourceGuid,
                                     SpriteMaterialAssetData& outAsset, std::string& outError);

} // namespace HIKARI::SPRITE
