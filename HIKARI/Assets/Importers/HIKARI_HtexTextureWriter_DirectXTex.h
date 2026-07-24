#pragma once

#include <filesystem>
#include <string>

#include "HIKARI_TextureImportSettings.h"

namespace HIKARI {

    bool WriteHtexFromDdsWithDirectXTex(
        const std::filesystem::path& ddsPath,
        const std::filesystem::path& htexPath,
        const TextureImportSettings& settings,
        std::string& outMessage);

} // namespace HIKARI
