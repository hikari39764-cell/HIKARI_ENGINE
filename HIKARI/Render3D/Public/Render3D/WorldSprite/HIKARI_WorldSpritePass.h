#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <d3d12.h>
#include "Render3D/Lighting/HIKARI_SurfaceLightingBindings.h"

#include "Gfx/HIKARI_GfxContext.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/DepthOfField/HIKARI_CameraDepthOfFieldStage.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {

struct WorldSpritePassStats {
    bool ready = false;
    bool meshShader = false;
    uint32_t submittedCount = 0u;
    uint32_t opaqueCount = 0u;
    uint32_t maskCount = 0u;
    uint32_t hybridCount = 0u;
    uint32_t transparentCount = 0u;
    uint32_t missingTextureCount = 0u;
    uint32_t droppedCount = 0u;
    uint32_t shadowCasterCount = 0u;
    uint64_t uploadedBytes = 0u;
    uint64_t residentBytes = 0u;
    uint32_t textureLoadRequests = 0u;
    bool depthOfFieldCaptureReady = false;
};

class WorldSpritePass final {
public:
    WorldSpritePass();
    ~WorldSpritePass();
    WorldSpritePass(const WorldSpritePass&) = delete;
    WorldSpritePass& operator=(const WorldSpritePass&) = delete;

    bool Initialize(const GFX::Context& context, uint32_t instanceCapacity = 4096u, bool preferRaster = false);
    void Shutdown() noexcept;
    bool PrepareFrame(const GFX::Context& context, const WorldSpriteFrameSource* source, const Camera3D& camera);
    bool ExecuteForwardOpaque(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                              const SurfaceLightingBindings& lighting = {});
    bool ExecuteForwardTransparent(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                                   const SurfaceLightingBindings& lighting = {});
    bool ExecuteForwardTransparentDepthOfField(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                                               const SurfaceLightingBindings& lighting,
                                               const DOF::WorldSpriteLayerCapture& capture);

    bool HasOpaqueWork() const noexcept;
    bool ExecuteDepth(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);
    bool ExecuteSurface(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants, bool featureMask);
    bool ExecuteShadow(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                       const MATH::Mat4& lightViewProjection);
    bool HasShadowWork() const noexcept;
    std::span<const MATH::Vec4> GetShadowCasterBounds() const noexcept;
    bool HasTransparentWork() const noexcept;
    bool HasFrameWork() const noexcept;
    const WorldSpritePassStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
