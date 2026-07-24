#pragma once

#include <filesystem>
#include <string>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

    bool WriteHmatFile(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& material,
        std::string& outMessage);

    bool ReadHmatFile(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outMaterial,
        std::string& outMessage);

} // namespace HIKARI
