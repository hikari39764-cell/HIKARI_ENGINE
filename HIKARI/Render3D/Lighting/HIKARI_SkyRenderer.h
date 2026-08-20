#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::SKYRENDERER {

    struct SkyRendererDebugState {
        bool initialized = false;
        bool lastRenderSubmitted = false;
        bool skyAssetFound = false;
        bool cubemapLoaded = false;
        bool usingFallback = false;
        bool textureValid = false;
        SkyMode mode = SkyMode::None;
        RENDER3D::TextureResourceHandle cubemapResource{};
        RENDER3D::TextureResourceHandle textureResource{};
        int cubemapHandle = -1;
        int textureHandle = -1;
        uint32_t constantFrameSlot = 0;
        uint32_t constantDrawSlot = 0;
        uint32_t constantDrawCountThisFrame = 0;
        uint32_t constantCapacityPerFrame = 0;
        size_t psoCreateCount = 0;
        size_t drawCount = 0;
        size_t constantOverflowCount = 0;
        std::string activeSkyAsset{};
        std::string activeTexturePath{};
    };

    struct SkyEnvironmentData {
        bool valid = false;
        bool hasCubemap = false;
        bool usingFallback = false;
        SkyMode mode = SkyMode::None;
        RENDER3D::TextureResourceHandle cubemapResource{};
        RENDER3D::TextureResourceHandle textureResource{};
        int cubemapHandle = -1;
        int textureHandle = -1;
        D3D12_GPU_DESCRIPTOR_HANDLE cubemapSrv{};
        MATH::Vec3 zenithColor{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 horizonColor{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 groundColor{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
        float exposure = 1.0f;
        float ambientFromSky = 0.0f;
        float reflectionIntensity = 0.0f;
        float horizonPower = 1.0f;
        float yaw = 0.0f;
        std::string activeSkyAsset{};
        std::string activeTexturePath{};
        bool atmosphereEnabled = false;
        bool atmosphereLutsReady = false;
        bool atmosphereAnalyticFallback = false;
        MATH::Vec3 sunDirection{ 0.0f, 1.0f, 0.0f };
        MATH::Vec3 sunSourceColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunTransmittance{ 1.0f, 1.0f, 1.0f };
        // Source radiance drives atmosphere integration.  Observed radiance
        // drives the disk/glare only; directional light intensity remains the
        // sole direct surface-light authority.
        float sunSourceRadiance = 0.0f;
        float sunRadiance = 0.0f;
        MATH::Vec3 rayleighScattering{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 mieScattering{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 ozoneAbsorption{ 0.0f, 0.0f, 0.0f };
        float mieAnisotropy = 0.0f;
        float planetRadius = 0.0f;
        float atmosphereHeight = 0.0f;
        float rayleighScaleHeight = 0.0f;
        float mieScaleHeight = 0.0f;
        float ozoneCenterHeight = 0.0f;
        float ozoneWidth = 0.0f;
        float groundAlbedo = 0.0f;
        float sunIlluminance = 1.0f;
        float sunExposure = 0.0f;
        float aerialPerspectiveStrength = 1.0f;
        float sunAngularRadius = 0.004675f;
        SunGlareSettings sunGlare{};
    };

    // Narrow renderer-to-post contract. Direction and radiance are derived
    // from SceneEnvironment::directional; screen position is a probe input,
    // not a second light owner.
    struct AtmosphereFrameData {
        bool valid = false;
        bool atmosphereEnabled = false;
        bool lutsReady = false;
        bool usingAnalyticFallback = false;
        MATH::Vec3 sunDirection{ 0.0f, 1.0f, 0.0f };
        MATH::Vec3 sunSourceColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunTransmittance{ 1.0f, 1.0f, 1.0f };
        float sunSourceRadiance = 0.0f;
        float sunRadiance = 0.0f;
        float sunAngularRadius = 0.004675f;
        MATH::Vec2 screenSunPosition{ 0.5f, 0.5f };
        bool screenSunVisible = false;
        SunGlareSettings sunGlare{};
    };

    void Reset();
    // Runtime/device teardown hook for the atmosphere owner. Reset() is a
    // per-frame debug-state reset and intentionally preserves atmosphere LUTs
    // and PSOs. The host must call this before the shared descriptor pool is
    // destroyed; it does not tear down the legacy sky renderer resources.
    void ShutdownAtmosphereRuntime();
    void InvalidateSkyTextureCache();
    void Render(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        ModelManager& modelManager,
        SkyManager& skyManager);
    const SkyRendererDebugState& GetDebugState();
    const SkyEnvironmentData& GetEnvironmentData();
    AtmosphereFrameData BuildAtmosphereFrameData(
        const SceneEnvironment& environment);
    AtmosphereFrameData BuildAtmosphereFrameData(
        const SceneEnvironment& environment,
        const Camera3D& camera);
    const AtmosphereFrameData& GetAtmosphereFrameData();
    D3D12_GPU_DESCRIPTOR_HANDLE GetActiveCubemapSrv();
    int GetActiveCubemapHandle();
    bool HasActiveCubemap();

} // namespace HIKARI::SKYRENDERER
