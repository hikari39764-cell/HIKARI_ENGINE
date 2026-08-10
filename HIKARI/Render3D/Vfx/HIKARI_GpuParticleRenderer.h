#pragma once

#include <cstdint>
#include <memory>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI {

    class AssetRegistry;
    class Camera3D;
    class Material;

    namespace RENDER3D::MATERIAL {
        struct MaterialExternalValueSnapshot;
    }

    namespace RENDER3D::PARTICLES {

        struct GpuParticleRendererStats {
            bool ready = false;
            uint32_t particleCapacity = 0u;
            uint32_t allocatedParticles = 0u;
            uint32_t activeEmitters = 0u;
            uint32_t simulatedEmitters = 0u;
            uint32_t drawnEmitters = 0u;
            uint32_t spawnedParticles = 0u;
            uint32_t allocationFailures = 0u;
            uint32_t simulationFailures = 0u;
            uint32_t emitterUploadCapacity = 0u;
            uint32_t emitterUploadsUsed = 0u;
            uint32_t emitterUploadBudgetFailures = 0u;
            uint32_t materialUploadFailures = 0u;
        };

        struct ParticleRenderViewExtent {
            uint32_t width = 0u;
            uint32_t height = 0u;

            bool IsValid() const noexcept {
                return width > 0u && height > 0u;
            }
        };

        class GpuParticleRenderer final {
        public:
            GpuParticleRenderer();
            ~GpuParticleRenderer();

            GpuParticleRenderer(const GpuParticleRenderer&) = delete;
            GpuParticleRenderer& operator=(
                const GpuParticleRenderer&) = delete;

            bool Initialize(
                const GFX::Context& context,
                uint32_t particleCapacity = kGpuParticlePoolCapacity,
                uint32_t emitterCapacity =
                    kDefaultParticleEmitterUploadPageCapacity);
            void UpdateContext(const GFX::Context& context) noexcept;
            void Shutdown();

            // The same renderer may be called for the scene view and tool
            // previews. An emitter key is simulated at most once per frame,
            // while every call can draw it through a different camera/target.
            bool Render(
                const ParticleRenderInput& input,
                const Camera3D& camera,
                const AssetRegistry* assetRegistry = nullptr,
                const RENDER3D::MATERIAL::MaterialExternalValueSnapshot*
                    externalValues = nullptr,
                ParticleRenderViewExtent viewExtent = {});
            // Releases every emitter allocation owned by a preview or scene
            // playback namespace. Call on the render/main thread.
            void ReleaseNamespace(uint64_t playbackNamespace);

            const GpuParticleRendererStats& GetStats() const noexcept;

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_{};
        };

    } // namespace RENDER3D::PARTICLES
} // namespace HIKARI
