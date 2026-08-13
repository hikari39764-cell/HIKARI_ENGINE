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
            gMeshRendererState.gpuDrivenSceneResidency.Reset();
            gMeshRendererState.gpuDrivenLayer.ResetFrame();
        }
    }

    void Reset() {
        gMeshRendererState.frameObjectIndex = 0;
        gMeshRendererState.gpuMaterialRegistry.Clear();
        gMeshRendererState.gpuDrivenSceneSource.Reset();
        gMeshRendererState.gpuDrivenSceneSourceIdentity = nullptr;
        gMeshRendererState.particleFrameSource = nullptr;
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

    bool HasSubmittedItems() {
        return HasGpuDrivenSceneSource() ||
            (gMeshRendererState.particleFrameSource != nullptr &&
             gMeshRendererState.particleFrameSource->HasFrameWork());
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
        return BeginFrameInternal(
            graphics,
            camera,
            environment,
            lighting,
            screenWidth,
            screenHeight,
            debugView,
            cameraOverrides);
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
        return GetActiveMeshFrameResources().cameraMapped;
    }

    const CameraCB* GetGpuDrivenCullingCameraConstants() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.cullingCameraMapped != nullptr
            ? frame.cullingCameraMapped
            : frame.cameraMapped;
    }

    RENDER3D::LIGHTING::LocalLightListGpuBindings
        GetLocalLightListGpuBindings() {

        const MeshFrameResources& frame =
            GetActiveMeshFrameResources();
        RENDER3D::LIGHTING::LocalLightListGpuBindings bindings{};
        bindings.lights = frame.localLightBuffer
            ? frame.localLightBuffer->GetGPUVirtualAddress()
            : 0u;
        bindings.clusterHeaders =
            frame.localLightClusterHeaderBuffer
                ? frame.localLightClusterHeaderBuffer->
                    GetGPUVirtualAddress()
                : 0u;
        bindings.clusterIndices =
            frame.localLightClusterIndexBuffer
                ? frame.localLightClusterIndexBuffer->
                    GetGPUVirtualAddress()
                : 0u;
        bindings.view = frame.localLightListView;
        return bindings;
    }

    void EndFrame() {
        gMeshRendererState.frameObjectIndex = 0;
    }

    void RenderAll(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return;
        }
        SetParticleFrameSource(view.particleFrameSource);
        (void)RENDER3D::PIPELINE::RenderMeshLightingFrame(
            graphics,
            view,
            environment,
            lighting,
            debugView);
        SetParticleFrameSource(nullptr);
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

    const RENDER3D::PARTICLES::ParticleGpuDrivenPassStats&
        GetParticleGpuDrivenPassStats() {
        return gMeshRendererState.particlePass.GetStats();
    }

} // namespace HIKARI::MESHRENDERER
