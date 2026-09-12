#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"

#include <filesystem>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyAsset.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {

        IBL::IblEnvironmentSource gDefaultSkyIblSource{};

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

    DefaultSkyEnvironmentResult EnsureDefaultSkyEnvironmentAvailable(
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

        if (!Exists(skyPath) ||
            !Exists(irradiancePath) ||
            !Exists(prefilteredPath) ||
            !Exists(brdfPath)) {
            gDefaultSkyIblSource = {};
            result.message =
                "Engine DefaultSky requires one cubemap and a complete IBL set; "
                "one or more files are unavailable under " +
                skyPath.parent_path().generic_string();
            return result;
        }

        const std::string skyTexturePath = skyPath.generic_string();
        const SkyAsset* registeredSky =
            skyManager.FindAsset(kDefaultSkyAssetName);
        if (registeredSky == nullptr ||
            registeredSky->texturePath != skyTexturePath ||
            registeredSky->preferredMode != SkyMode::Cubemap ||
            !registeredSky->textureIsLinear) {
            skyManager.RegisterOrUpdateAsset(SkyAsset{
                kDefaultSkyAssetName,
                {},
                skyTexturePath,
                SkyMode::Cubemap,
                true
            });
        }
        result.skyRegistered = true;
        result.skyPath = skyTexturePath;

        if (!IsTextureResourceValid(
                gDefaultSkyIblSource.irradianceResource)) {
            gDefaultSkyIblSource.irradianceResource = LoadCubemapResource(
                "ibl/default_sky/irradiance",
                irradiancePath.generic_string(),
                TextureResourceColorSpace::Linear);
        }
        if (!IsTextureResourceValid(
                gDefaultSkyIblSource.prefilteredResource)) {
            gDefaultSkyIblSource.prefilteredResource = LoadCubemapResource(
                "ibl/default_sky/prefiltered",
                prefilteredPath.generic_string(),
                TextureResourceColorSpace::Linear);
        }
        if (!IsTextureResourceValid(
                gDefaultSkyIblSource.brdfLutResource)) {
            gDefaultSkyIblSource.brdfLutResource = LoadTextureResourceLinear(
                "ibl/default_sky/brdf_lut",
                brdfPath.generic_string());
        }
        gDefaultSkyIblSource.prefilteredMipCount = 7u;
        result.iblLoaded = gDefaultSkyIblSource.IsComplete();
        if (!result.iblLoaded) {
            // A partial environment can combine a current sky with stale
            // fallback descriptors. Preview/runtime may only bind the set as
            // one coherent unit.
            gDefaultSkyIblSource = {};
        }
        result.message = result.iblLoaded
            ? "Engine DefaultSky Cubemap and IBL loaded."
            : "Engine DefaultSky Cubemap registered; IBL resources are unavailable.";
        return result;
    }

    DefaultSkyEnvironmentResult ActivateDefaultSkyEnvironment(
        SkyManager& skyManager) {

        DefaultSkyEnvironmentResult result =
            EnsureDefaultSkyEnvironmentAvailable(skyManager);
        if (!result.skyRegistered) {
            IBL::Reset();
            return result;
        }

        result.iblLoaded = IBL::SetFromTextureResources(
            gDefaultSkyIblSource.irradianceResource,
            gDefaultSkyIblSource.prefilteredResource,
            gDefaultSkyIblSource.brdfLutResource,
            gDefaultSkyIblSource.prefilteredMipCount);
        if (!result.iblLoaded) IBL::Reset();
        return result;
    }

    const IBL::IblEnvironmentSource& GetDefaultSkyIblSource() noexcept {
        return gDefaultSkyIblSource;
    }

} // namespace HIKARI::RENDER3D::LIGHTING
