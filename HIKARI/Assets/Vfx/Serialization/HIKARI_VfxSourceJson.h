#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::SERIALIZATION {

bool ParseVfxSourceJson(std::string_view jsonText, VfxAssetData& outAsset, std::string& outError);
bool SerializeVfxSourceJson(const VfxAssetData& asset, std::string& outJsonText, std::string& outError);
bool LoadVfxSourceJson(const std::filesystem::path& path, VfxAssetData& outAsset, std::string& outError);
bool SaveVfxSourceJson(const std::filesystem::path& path, const VfxAssetData& asset, std::string& outError);

} // namespace HIKARI::VFX::SERIALIZATION
