#pragma once

#include <filesystem>
#include <string>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI {

    bool WriteHmodelFile(
        const std::filesystem::path& path,
        const ModelAsset& model,
        std::string& outMessage);

    bool ReadHmodelFile(
        const std::filesystem::path& path,
        ModelAsset& outModel,
        std::string& outMessage);

} // namespace HIKARI
