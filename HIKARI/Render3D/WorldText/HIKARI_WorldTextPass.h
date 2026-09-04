#pragma once

#include <cstdint>
#include <memory>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"

namespace HIKARI::RENDER3D::WORLD_TEXT {

    struct WorldTextPassStats {
        bool ready = false;
        uint32_t requestedTextCount = 0u;
        uint32_t submittedTextCount = 0u;
        uint32_t submittedGlyphCount = 0u;
        uint32_t failedTextRequestCount = 0u;
        uint32_t missingAtlasTextureCount = 0u;
        uint32_t droppedTextCount = 0u;
        uint32_t droppedGlyphCount = 0u;
        uint32_t dispatchCount = 0u;
    };

    class WorldTextPass final {
    public:
        WorldTextPass();
        ~WorldTextPass();
        WorldTextPass(const WorldTextPass&) = delete;
        WorldTextPass& operator=(const WorldTextPass&) = delete;

        bool Initialize(
            const GFX::Context& context,
            uint32_t glyphCapacity =
                TEXT_RENDERING::kSdfTextDefaultMaximumGlyphCount);
        void Shutdown() noexcept;
        bool PrepareFrame(
            const GFX::Context& context,
            const WorldTextFrameSource* source,
            const MATH::Vec3& cameraPosition);
        bool ExecuteForwardTransparent(
            const GFX::Context& context,
            D3D12_GPU_VIRTUAL_ADDRESS cameraConstants);

        bool HasTransparentWork() const noexcept;
        bool HasFrameWork() const noexcept;
        const WorldTextPassStats& GetStats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };

} // namespace HIKARI::RENDER3D::WORLD_TEXT
