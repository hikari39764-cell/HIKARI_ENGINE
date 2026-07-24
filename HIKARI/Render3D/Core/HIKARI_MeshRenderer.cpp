#include "HIKARI_MeshRenderer.h"

#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::MESHRENDERER {

    using INTERNAL::BeginFrameInternal;
    using INTERNAL::GetActiveMeshFrameResources;
    using INTERNAL::gMeshRendererState;
    using INTERNAL::HasGpuDrivenSceneSource;

    void Reset() {
        gMeshRendererState.frameObjectIndex = 0;
        gMeshRendererState.gpuMaterialRegistry.Clear();
        gMeshRendererState.gpuDrivenSceneSource.Reset();
        gMeshRendererState.gpuDrivenSceneSourceIdentity = nullptr;
        gMeshRendererState.freezeGpuDrivenCullingCamera = false;
        gMeshRendererState.frozenCullingCamera = {};
        gMeshRendererState.frozenCullingCameraValid = false;
        gMeshRendererState.frozenCullingCameraWidth = 0;
        gMeshRendererState.frozenCullingCameraHeight = 0;
        gMeshRendererState.debugStats = {};
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
            return;
        }
        gMeshRendererState.gpuDrivenSceneSource = *source;
        gMeshRendererState.traditionalIndirectOwner.CopyFromSceneSource(
            gMeshRendererState.gpuDrivenSceneSource);
        gMeshRendererState.traditionalIndirectOwner.AttachToSceneSource(
            gMeshRendererState.gpuDrivenSceneSource);
    }

    bool HasSubmittedItems() {
        return HasGpuDrivenSceneSource();
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {
        return BeginFrameInternal(
            camera,
            environment,
            0u,
            0u,
            debugView,
            nullptr);
    }

    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {
        return BeginFrameInternal(
            camera,
            environment,
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

    void EndFrame() {
        gMeshRendererState.frameObjectIndex = 0;
    }

    void RenderAll(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return;
        }
        (void)RENDER3D::PIPELINE::RenderMeshLightingFrame(view, environment, debugView);
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

} // namespace HIKARI::MESHRENDERER
