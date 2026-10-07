#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include "Render3D/Lighting/HIKARI_SurfaceLightingBindings.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::MATERIAL {
class GpuMaterialRegistry;
}

namespace HIKARI::RENDER3D::PARTICLES {

struct ParticleGpuDrivenPassStats {
    bool ready = false;
    uint32_t particleCapacity = 0u;
    uint32_t allocatedParticles = 0u;
    uint32_t activeEmitters = 0u;
    uint32_t simulatedEmitters = 0u;
    uint32_t simulationSkippedEmitters = 0u;
    uint32_t compactedEmitters = 0u;
    uint32_t compactionReuseFrames = 0u;
    uint32_t drawnEmitters = 0u;
    uint32_t spawnedParticles = 0u;
    uint32_t allocationFailures = 0u;
    uint32_t simulationFailures = 0u;
    uint32_t materialFailures = 0u;
    uint32_t lightingFallbackEmitters = 0u;
    uint32_t compactedCapacityThreads = 0u;
    uint32_t indirectDispatchCommands = 0u;
    uint32_t spriteDispatchCommands = 0u;
    uint32_t stripDispatchCommands = 0u;
    // True only when every submitted reset/spawn/material dependency was
    // accepted for this source snapshot. Preview delivery may ACK only
    // frames with this receipt.
    bool inputAccepted = false;
    // Independent simulation receipt used by the main runtime delivery
    // owner. Material or later screen-pass failures must not replay a
    // reset/spawn that was already recorded for this exact token.
    bool simulationAccepted = false;
    ParticleRenderDeliveryToken simulationAcceptedToken{};
};

struct ParticleGpuDrivenDrawBindings : SurfaceLightingBindings {
    D3D12_GPU_VIRTUAL_ADDRESS camera = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS materialData = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS materialParameters = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS materialResources = 0u;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth{};
    MATH::Vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    bool cameraPositionValid = false;
    PIPELINE::RenderTargetAccess compositionTarget{};
    D3D12_CPU_DESCRIPTOR_HANDLE compositionDepth{};
    uint32_t width = 0, height = 0;
    // Tool/secondary views without Global Post draw both routes in their local target.
    std::optional<VFX::VfxPostProcessMode> postProcessMode;
};

// Owned by one MeshRenderer view-resource set. This object has no
// target-owning Render entry point: each main or bounded secondary view
// prepares its own instance, then executes it from that view's
// ForwardTransparent pass without sharing camera-dependent GPU state.
class ParticleGpuDrivenPass final {
public:
    ParticleGpuDrivenPass();
    ~ParticleGpuDrivenPass();
    ParticleGpuDrivenPass(const ParticleGpuDrivenPass&) = delete;
    ParticleGpuDrivenPass& operator=(const ParticleGpuDrivenPass&) = delete;

    bool Initialize(const GFX::Context& context, uint32_t particleCapacity = kGpuParticlePoolCapacity,
                    uint32_t emitterPageCapacity = kDefaultParticleEmitterUploadPageCapacity);
    void Shutdown();
    bool PrepareFrame(const GFX::Context& context, const ParticleRenderFrameSource* source,
                      MATERIAL::GpuMaterialRegistry& materialRegistry);
    bool ExecuteForwardTransparent(const GFX::Context& context, const ParticleGpuDrivenDrawBindings& bindings);
    void ReleaseNamespace(uint64_t playbackNamespace);
    bool HasDrawWork(std::optional<VFX::VfxPostProcessMode> mode = std::nullopt) const noexcept;
    const ParticleGpuDrivenPassStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::PARTICLES
