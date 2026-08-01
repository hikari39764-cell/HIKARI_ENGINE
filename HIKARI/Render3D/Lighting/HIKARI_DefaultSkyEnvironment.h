#pragma once

#include <string>

namespace HIKARI {
    class SkyManager;
}

namespace HIKARI::RENDER3D::LIGHTING {

    inline constexpr const char* kDefaultSkyAssetName =
        "DefaultSky";

    struct DefaultSkyEnvironmentResult {
        bool skyRegistered = false;
        bool iblLoaded = false;
        std::string skyPath{};
        std::string message{};
    };

    DefaultSkyEnvironmentResult ActivateDefaultSkyEnvironment(
        SkyManager& skyManager);

} // namespace HIKARI::RENDER3D::LIGHTING
