#pragma once

#include <filesystem>
#include <string>

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D {

    std::filesystem::path RenderQualityProfilePath(
        const std::filesystem::path& projectRoot);

    bool LoadRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        RenderQualitySettings& outSettings,
        std::string* errorMessage = nullptr);

    bool SaveRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        const RenderQualitySettings& settings,
        std::string* errorMessage = nullptr);

} // namespace HIKARI::RENDER3D
