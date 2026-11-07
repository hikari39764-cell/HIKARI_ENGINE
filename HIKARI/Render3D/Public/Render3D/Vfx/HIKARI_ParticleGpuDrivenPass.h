#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include "Render3D/Lighting/HIKARI_SurfaceLightingBindings.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::FLUID {
struct FluidFlowView;
}

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
    // 歪み層だけを戦闘の画面歪み遮罩へ描く経路。色の経路は歪み層を描かない
    bool distortionMask = false;
    // この視図の流体の煙の相機体積（HIKARI_FluidCameraVolume.hlsli）。無い視図は空で、params.w = 0。
    D3D12_GPU_DESCRIPTOR_HANDLE fluidCameraVolume{};
    MATH::Vec4 fluidCameraVolumeParams{0.0f, 0.0f, 0.0f, 0.0f};
    // 主視图だけ真：この視図の深度（sceneDepth）と相機相対の viewProj（抖動なし、FLUID::MakeCameraRelativeViewProj）を、
    // 粒子の場景深度碰撞へ撮る（次のフレームの模拟が上一帧の深度として使う）。1 フレームに 1 度だけ撮る。
    bool captureCollisionDepth = false;
    MATH::Mat4 collisionViewProj{};
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
    // fluidFlow はこのフレームに解いた流体の流れ（fluidFollow の粒子が乗る。無ければ乗らない）。
    bool PrepareFrame(const GFX::Context& context, const ParticleRenderFrameSource* source,
                      MATERIAL::GpuMaterialRegistry& materialRegistry, const FLUID::FluidFlowView* fluidFlow = nullptr);
    // 直前の PrepareFrame が失敗した理由（成功時は空文字列）。
    const char* GetLastPrepareFailure() const noexcept;
    bool ExecuteForwardTransparent(const GFX::Context& context, const ParticleGpuDrivenDrawBindings& bindings);
    void ReleaseNamespace(uint64_t playbackNamespace);
    // distortion が真なら歪み層だけを数え、偽なら歪み層を除いて数える
    bool HasDrawWork(std::optional<VFX::VfxPostProcessMode> mode = std::nullopt,
                     bool distortion = false) const noexcept;
    // 直前の歪み経路が描いた遮罩（RG = UV のずれ、B = 覆い、A = ぼかし）。PostSystem へ渡す
    bool GetDistortionMask(RenderTextureView& outView, D3D12_CPU_DESCRIPTOR_HANDLE& outSrvCpu) const noexcept;
    const ParticleGpuDrivenPassStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::PARTICLES
