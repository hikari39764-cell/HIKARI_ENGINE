#include "HIKARI_MeshRenderer.h"

#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::MESHRENDERER {

    using INTERNAL::BeginFrameInternal;
    using INTERNAL::GetActiveMeshFrameResources;
    using INTERNAL::gMeshRendererState;
    using INTERNAL::HasGpuDrivenSceneSource;

    namespace {
        void InvalidateGpuDrivenSceneResidency() {
            gMeshRendererState.surfaceGpuSceneBuffer.InvalidateResidency();
            gMeshRendererState.massGpuSceneBuffer.InvalidateResidency();
            gMeshRendererState.gpuDrivenSceneResidency.Reset();
            gMeshRendererState.gpuDrivenLayer.ResetFrame();
        }
    }

    void Reset() {
        gMeshRendererState.materialFrameReady = false;
        gMeshRendererState.meshletRenderBackend.ResetVisibility();
        ResetSecondarySceneCaptureResources();
        gMeshRendererState.frameObjectIndex = 0;
        gMeshRendererState.gpuMaterialRegistry.Clear();
        gMeshRendererState.gpuDrivenSceneSource.Reset();
        gMeshRendererState.gpuDrivenSceneSourceIdentity = nullptr;
        gMeshRendererState.particleFrameSource = nullptr;
        gMeshRendererState.worldSpriteFrameSource = nullptr;
        gMeshRendererState.worldTextFrameSource = nullptr;
        InvalidateGpuDrivenSceneResidency();
        gMeshRendererState.freezeGpuDrivenCullingCamera = false;
        gMeshRendererState.frozenCullingCamera = {};
        gMeshRendererState.frozenCullingCameraValid = false;
        gMeshRendererState.frozenCullingCameraWidth = 0;
        gMeshRendererState.frozenCullingCameraHeight = 0;
        gMeshRendererState.debugStats = {};
    }

    void EnsureReadyForFirstFrame(const GFX::Context& graphics) {
        (void)INTERNAL::EnsureInitialized(graphics);
    }

    void InvalidateMaterialFxPipelineCache() {
        InvalidateMeshPipelineVariants(gMeshRendererState.pipelines);
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        gMeshRendererState.materialFrameReady = false;

        if (source != nullptr &&
            gMeshRendererState.gpuDrivenSceneSourceIdentity == source &&
            gMeshRendererState.gpuDrivenSceneSource.layoutVersion == source->layoutVersion &&
            gMeshRendererState.gpuDrivenSceneSource.sourceVersion == source->sourceVersion &&
            gMeshRendererState.gpuDrivenSceneSource.sourceInstanceCount == source->sourceInstanceCount &&
            gMeshRendererState.gpuDrivenSceneSource.sourceRecordCount == source->sourceRecordCount) {

            gMeshRendererState.gpuDrivenSceneSource.dirtyBaseSourceVersion =
                source->dirtyBaseSourceVersion;
            for (size_t passIndex = 0;
                passIndex < RENDER3D::GPUDRIVEN::kGpuDrivenPassCount;
                ++passIndex) {

                gMeshRendererState.gpuDrivenSceneSource.passes[passIndex].dirtyRanges =
                    source->passes[passIndex].dirtyRanges;
            }
            return;
        }

        gMeshRendererState.gpuDrivenSceneSource.Reset();
        gMeshRendererState.traditionalIndirectOwner.Clear();
        gMeshRendererState.gpuDrivenSceneSourceIdentity = source;
        if (source == nullptr) {
            gMeshRendererState.gpuMaterialRegistry.Clear();
            InvalidateGpuDrivenSceneResidency();
            return;
        }
        gMeshRendererState.gpuDrivenSceneSource = *source;
        gMeshRendererState.traditionalIndirectOwner.CopyFromSceneSource(
            gMeshRendererState.gpuDrivenSceneSource);
        gMeshRendererState.traditionalIndirectOwner.AttachToSceneSource(
            gMeshRendererState.gpuDrivenSceneSource);
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource&
        GetActiveGpuDrivenSceneSource() {
        return gMeshRendererState.gpuDrivenSceneSource;
    }

    void SetParticleFrameSource(
        const RENDER3D::PARTICLES::ParticleRenderFrameSource* source) {
        gMeshRendererState.particleFrameSource = source;
    }

    void SetWorldSpriteFrameSource(
        const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource* source) {
        gMeshRendererState.worldSpriteFrameSource = source;
    }

    void SetWorldTextFrameSource(
        const RENDER3D::WORLD_TEXT::WorldTextFrameSource* source) {
        gMeshRendererState.worldTextFrameSource = source;
    }

    void ReleaseParticleNamespace(uint64_t playbackNamespace) {
        if (playbackNamespace == 0u) return;
        gMeshRendererState.particlePass.ReleaseNamespace(playbackNamespace);
        gMeshRendererState.gpuMaterialRegistry.ReleaseSourceBindings(
            RENDER3D::PARTICLES::MakeParticleMaterialSourceIdentity(
                playbackNamespace,
                nullptr));
    }

    void ShutdownParticleGpuDrivenPass() {
        gMeshRendererState.particlePass.Shutdown();
        gMeshRendererState.particleFrameSource = nullptr;
    }

    void ShutdownOceanSpectrumRuntime() {
        const GFX::Context& graphics = GFX::GetCurrentContext();
        gMeshRendererState.oceanSpectrumRuntime.Shutdown(
            graphics.device != nullptr ? &graphics : nullptr);
        INTERNAL::ResetOceanSpectrumLazyInitializationState();
    }

    void ShutdownWorldSpritePass() {
        gMeshRendererState.worldSpritePass.Shutdown();
        gMeshRendererState.worldSpriteFrameSource = nullptr;
    }

    void ShutdownWorldTextPass() {
        gMeshRendererState.worldTextPass.Shutdown();
        gMeshRendererState.worldTextFrameSource = nullptr;
    }

    bool HasSubmittedItems() {
        return HasGpuDrivenSceneSource() ||
            (gMeshRendererState.particleFrameSource != nullptr &&
             gMeshRendererState.particleFrameSource->HasFrameWork()) ||
            (gMeshRendererState.worldSpriteFrameSource != nullptr &&
             gMeshRendererState.worldSpriteFrameSource->HasFrameWork()) ||
            (gMeshRendererState.worldTextFrameSource != nullptr &&
             gMeshRendererState.worldTextFrameSource->HasFrameWork());
    }

    bool BeginFrame(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {
        gMeshRendererState.materialFrameReady = false;
        const bool ready = BeginFrameInternal(
            graphics,
            camera,
            environment,
            lighting,
            screenWidth,
            screenHeight,
            debugView,
            cameraOverrides);
        gMeshRendererState.materialFrameContext = graphics;
        gMeshRendererState.materialFrameReady = ready;
        return ready;
    }

    void SetGpuDrivenCullingCameraFreezeEnabled(bool enabled) {
        if (gMeshRendererState.freezeGpuDrivenCullingCamera == enabled) {
            return;
        }
        gMeshRendererState.freezeGpuDrivenCullingCamera = enabled;
        if (!enabled) {
            gMeshRendererState.frozenCullingCamera = {};
            gMeshRendererState.frozenCullingCameraValid = false;
            gMeshRendererState.frozenCullingCameraWidth = 0;
            gMeshRendererState.frozenCullingCameraHeight = 0;
            gMeshRendererState.debugStats.gpuDrivenCullingCameraFrozen = false;
        }
    }

    bool IsGpuDrivenCullingCameraFrozen() {
        return gMeshRendererState.freezeGpuDrivenCullingCamera && gMeshRendererState.frozenCullingCameraValid;
    }

    const CameraCB* GetCameraConstants() {
        const auto& view = GetActiveMeshFrameResources().view;
        return view.IsInitialized() ? &view.camera : nullptr;
    }

    bool FinalizeVolumetricFog(const GFX::Context& graphics, bool volumetricFogReady) {
        auto& view = GetActiveMeshFrameResources().view;
        if (!volumetricFogReady || view.light.fogParams.x == 0.0f) return true;
        view.light.fogParams.x = 0.0f;
        return view.PublishLight(graphics);
    }

    const CameraCB* GetGpuDrivenCullingCameraConstants() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.view.IsInitialized() ? &frame.view.cullingCamera : nullptr;
    }

    MeshMaterialGpuFrameView GetMaterialGpuFrameView(const GFX::Context& graphics) {
        const auto& published = gMeshRendererState.materialFrameContext;
        if (!gMeshRendererState.materialFrameReady || published.device != graphics.device ||
            published.deviceGeneration != graphics.deviceGeneration || published.cmdList != graphics.cmdList ||
            published.frameIndex != graphics.frameIndex || published.frameFence != graphics.frameFence ||
            published.currentFrameRetireFenceValue != graphics.currentFrameRetireFenceValue) return {};
        const auto& frame = GetActiveMeshFrameResources();
        if (!frame.materialDataBuffer || !frame.materialProgramParameterBuffer || !frame.materialProgramResourceBuffer ||
            frame.materialUploadNeedsRetry || frame.materialProgramUploadNeedsRetry) return {};
        const uintptr_t identity = reinterpret_cast<uintptr_t>(gMeshRendererState.gpuDrivenSceneSourceIdentity);
        const auto& registry = gMeshRendererState.gpuMaterialRegistry;
        return {registry.GetSourceBindings(identity), frame.materialDataStaging, frame.materialDataBuffer->GetGPUVirtualAddress(),
            frame.materialProgramParameterBuffer->GetGPUVirtualAddress(), frame.materialProgramResourceBuffer->GetGPUVirtualAddress(),
            registry.GetStats().dataVersion, registry.GetBindingVersion(identity),
            static_cast<uint32_t>(frame.materialDataStaging.size())};
    }

    bool RefreshLightingProviders(const GFX::Context& graphics,
        const RENDER3D::LIGHTING::CloudSolarVisibilityGpuData& clouds) {
        auto& view = GetActiveMeshFrameResources().view;
        FillSelectedIrradianceVolumes(view.skyEnvironment);
        view.skyEnvironment.cloudSolarVisibility=clouds;
        return view.PublishSkyEnvironment(graphics);
    }

    RENDER3D::LIGHTING::LocalLightListGpuBindings
        GetLocalLightListGpuBindings() {

        const MeshFrameResources& frame =
            GetActiveMeshFrameResources();
        return frame.view.Bindings().localLights;
    }

    void EndFrame() {
        gMeshRendererState.materialFrameReady = false;
        gMeshRendererState.frameObjectIndex = 0;
    }

    MeshRendererFrameResult RenderAll(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return {};
        }
        const bool massSubmissionRequired =
            gMeshRendererState.gpuDrivenSceneSource.HasMassInstances();
        gMeshRendererState.massForwardOpaqueSubmissionCommitted = false;
        SetParticleFrameSource(view.particleFrameSource);
        SetWorldSpriteFrameSource(view.worldSpriteFrameSource);
        SetWorldTextFrameSource(view.worldTextFrameSource);
        const bool rendered =
            RENDER3D::PIPELINE::RenderMeshLightingFrame(
                graphics,
                view,
                environment,
                lighting,
                debugView);
        SetParticleFrameSource(nullptr);
        SetWorldSpriteFrameSource(nullptr);
        SetWorldTextFrameSource(nullptr);
        return MeshRendererFrameResult{
            rendered,
            massSubmissionRequired,
            rendered &&
                gMeshRendererState.massForwardOpaqueSubmissionCommitted
        };
    }

    const MeshRendererDebugStats& GetDebugStats() {
        const MaterialFxProfileCacheStats fxCacheStats = MaterialFxProfile::GetCacheStats();
        gMeshRendererState.debugStats.materialFxProfileCacheHitCount = fxCacheStats.hitCount;
        gMeshRendererState.debugStats.materialFxProfileCacheMissCount = fxCacheStats.missCount;
        gMeshRendererState.debugStats.materialFxProfileCacheFailCount = fxCacheStats.failCount;
        return gMeshRendererState.debugStats;
    }

    const RENDER3D::MATERIAL::GpuMaterialRegistryStats&
        GetGpuMaterialRegistryStats() {
        return gMeshRendererState.gpuMaterialRegistry.GetStats();
    }

    const RENDER3D::GPUDRIVEN::MassGpuSceneFrameBufferStats&
        GetMassGpuSceneFrameBufferStats() {
        return gMeshRendererState.massGpuSceneBuffer.GetStats();
    }

    const RENDER3D::PARTICLES::ParticleGpuDrivenPassStats&
        GetParticleGpuDrivenPassStats() {
        return gMeshRendererState.particlePass.GetStats();
    }

    const RENDER3D::WORLD_SPRITES::WorldSpritePassStats&
        GetWorldSpritePassStats() {
        return gMeshRendererState.worldSpritePass.GetStats();
    }

    const RENDER3D::WORLD_TEXT::WorldTextPassStats&
        GetWorldTextPassStats() {
        return gMeshRendererState.worldTextPass.GetStats();
    }

} // namespace HIKARI::MESHRENDERER
