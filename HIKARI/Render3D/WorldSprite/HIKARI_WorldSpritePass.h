#pragma once

#include <cstdint>
#include <memory>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {

    struct WorldSpritePassStats {
        bool ready = false;
        uint32_t submittedCount = 0u;
        uint32_t opaqueCount = 0u;
        uint32_t maskCount = 0u;
        uint32_t hybridCount = 0u;
        uint32_t transparentCount = 0u;
        uint32_t missingTextureCount = 0u;
        uint32_t droppedCount = 0u;
    };

    class WorldSpritePass final {
    public:
        WorldSpritePass();
        ~WorldSpritePass();
        WorldSpritePass(const WorldSpritePass&) = delete;
        WorldSpritePass& operator=(const WorldSpritePass&) = delete;

        bool Initialize(
            const GFX::Context& context,
            uint32_t instanceCapacity = 4096u);
        void Shutdown() noexcept;
        bool PrepareFrame(
            const GFX::Context& context,
            const WorldSpriteFrameSource* source,
            const MATH::Vec3& cameraPosition);
        bool ExecuteForwardOpaque(
            const GFX::Context& context,
            D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);
        bool ExecuteForwardTransparent(
            const GFX::Context& context,
            D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);

        bool HasOpaqueWork() const noexcept;
        bool HasTransparentWork() const noexcept;
        bool HasFrameWork() const noexcept;
        const WorldSpritePassStats& GetStats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
