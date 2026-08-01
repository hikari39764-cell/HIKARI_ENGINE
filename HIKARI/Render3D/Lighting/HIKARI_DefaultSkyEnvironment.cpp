#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"

#include <filesystem>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyAsset.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {

        std::filesystem::path ResolveEngineSkyPath(
            const std::filesystem::path& relativePath) {

            return CONTENT::GetEngineContentContext().Resolve(
                std::filesystem::path("Skies") /
                relativePath);
        }

        bool Exists(const std::filesystem::path& path) {
            std::error_code error{};
            return !path.empty() &&
                std::filesystem::is_regular_file(path, error);
        }

    } // namespace

    DefaultSkyEnvironmentResult ActivateDefaultSkyEnvironment(
        SkyManager& skyManager) {

        DefaultSkyEnvironmentResult result{};
        const std::filesystem::path skyPath =
            ResolveEngineSkyPath("sky1.dds");
        const std::filesystem::path irradiancePath =
            ResolveEngineSkyPath("IBL/sky1_irradiance.dds");
        const std::filesystem::path prefilteredPath =
            ResolveEngineSkyPath("IBL/sky1_prefiltered.dds");
        const std::filesystem::path brdfPath =
            ResolveEngineSkyPath("IBL/brdf_lut.dds");

        if (!Exists(skyPath)) {
            IBL::Reset();
            result.message =
                "Engine default sky is unavailable: " +
                skyPath.generic_string();
            return result;
        }

        skyManager.RegisterOrUpdateAsset(SkyAsset{
            kDefaultSkyAssetName,
            {},
            skyPath.generic_string(),
            SkyMode::Cubemap,
            true
        });
        result.skyRegistered = true;
        result.skyPath = skyPath.generic_string();

        TextureResourceHandle irradiance{};
        TextureResourceHandle prefiltered{};
        TextureResourceHandle brdf{};
        if (Exists(irradiancePath)) {
            irradiance = LoadCubemapResource(
                "ibl/default_sky/irradiance",
                irradiancePath.generic_string(),
                TextureResourceColorSpace::Linear);
        }
        if (Exists(prefilteredPath)) {
            prefiltered = LoadCubemapResource(
                "ibl/default_sky/prefiltered",
                prefilteredPath.generic_string(),
                TextureResourceColorSpace::Linear);
        }
        if (Exists(brdfPath)) {
            brdf = LoadTextureResourceLinear(
                "ibl/default_sky/brdf_lut",
                brdfPath.generic_string());
        }

        IBL::SetFromTextureResources(
            irradiance,
            prefiltered,
            brdf,
            7u);
        result.iblLoaded =
            IsTextureResourceValid(irradiance) ||
            IsTextureResourceValid(prefiltered) ||
            IsTextureResourceValid(brdf);
        result.message = result.iblLoaded
            ? "Engine DefaultSky Cubemap and IBL loaded."
            : "Engine DefaultSky Cubemap registered; IBL resources are unavailable.";
        return result;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
