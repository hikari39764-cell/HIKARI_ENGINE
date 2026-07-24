#pragma once

#include <d3d12.h>
#include <string>

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
        size_t psoCreateCount = 0;
        size_t drawCount = 0;
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
        float exposure = 1.0f;
        float ambientFromSky = 0.0f;
        float reflectionIntensity = 0.0f;
        float horizonPower = 1.0f;
        float yaw = 0.0f;
        std::string activeSkyAsset{};
        std::string activeTexturePath{};
    };

    void Reset();
    void InvalidateSkyTextureCache();
    void Render(const Camera3D& camera, const SceneEnvironment& environment, ModelManager& modelManager, SkyManager& skyManager);
    const SkyRendererDebugState& GetDebugState();
    const SkyEnvironmentData& GetEnvironmentData();
    D3D12_GPU_DESCRIPTOR_HANDLE GetActiveCubemapSrv();
    int GetActiveCubemapHandle();
    bool HasActiveCubemap();

} // namespace HIKARI::SKYRENDERER
