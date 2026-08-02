#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_IblEnvironment.h"

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

    DefaultSkyEnvironmentResult EnsureDefaultSkyEnvironmentAvailable(
        SkyManager& skyManager);

    DefaultSkyEnvironmentResult ActivateDefaultSkyEnvironment(
        SkyManager& skyManager);

    const IBL::IblEnvironmentSource& GetDefaultSkyIblSource() noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
