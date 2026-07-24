#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <algorithm>

namespace HIKARI::SHADOW::INTERNAL {

    void InvalidateShadowSourceCache() {
        gShadowRendererState.shadowSourceCache.Invalidate();
    }

    bool CanReuseShadowSourceCache() {
        return gShadowRendererState.shadowSourceCache.Matches(gShadowRendererState.gpuDrivenSceneSource);
    }

    void PublishShadowCacheStats() {
        gShadowRendererState.shadowCache.PublishStats(gShadowRendererState.debugStats);
    }

    void InvalidateShadowCache() {
        gShadowRendererState.shadowCache.Invalidate();
        PublishShadowCacheStats();
    }

    bool HasStaticShadowDirtyRanges() {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
            gShadowRendererState.staticShadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        if (shadowPass.instances == nullptr ||
            shadowPass.instances->empty() ||
            shadowPass.dirtyRanges.empty()) {
            return false;
        }

        for (const RENDER3D::GPUDRIVEN::GpuSceneDirtyRange& range :
            shadowPass.dirtyRanges) {
            const size_t first = static_cast<size_t>(range.firstInstance);
            const size_t last = (std::min)(
                first + static_cast<size_t>(range.instanceCount),
                shadowPass.instances->size());
            for (size_t instanceIndex = first; instanceIndex < last; ++instanceIndex) {
                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    (*shadowPass.instances)[instanceIndex];
                const uint32_t staticFlag = static_cast<uint32_t>(
                    RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry);
                if ((instance.flags & staticFlag) != 0u) {
                    return true;
                }
            }
        }

        return false;
    }

    bool CanReuseShadowCache(uint32_t resolution) {
        ShadowCacheReuseInput input{};
        input.key.layoutVersion = gShadowRendererState.staticShadowSceneSource.layoutVersion;
        input.key.sourceVersion = gShadowRendererState.staticShadowSceneSource.sourceVersion;
        input.key.sourceInstanceCount =
            gShadowRendererState.staticShadowSceneSource.sourceInstanceCount;
        input.key.resolution = resolution;
        input.key.lightViewProj = gShadowRendererState.lightViewProj;
        input.hasStaticWork = gShadowRendererState.frameHasStaticShadowWork;
        input.resourcesReady =
            gShadowRendererState.staticShadowMap != nullptr &&
            gShadowRendererState.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(gShadowRendererState.shadowSrvResource);
        input.stateReusable =
            gShadowRendererState.staticShadowState == D3D12_RESOURCE_STATE_COPY_SOURCE ||
            gShadowRendererState.staticShadowState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        input.hasStaticDirtyRanges = HasStaticShadowDirtyRanges();
        return gShadowRendererState.shadowCache.EvaluateReuse(input);
    }

    size_t StatDelta(size_t after, size_t before) {
        return after >= before ? after - before : after;
    }

    void AccumulateShadowMeshletStatsDelta(
        const RENDER3D::MESHLET::MeshletRenderBackendStats& before,
        const RENDER3D::MESHLET::MeshletRenderBackendStats& after) {

        gShadowRendererState.debugStats.shadowMeshletRequestedDispatchCount +=
            StatDelta(after.requestedDispatchCount, before.requestedDispatchCount);
        gShadowRendererState.debugStats.shadowMeshletSubmittedDispatchCount +=
            StatDelta(after.shadowSubmittedDispatchCount, before.shadowSubmittedDispatchCount);
        gShadowRendererState.debugStats.shadowMeshletSkippedDispatchCount +=
            StatDelta(after.skippedDispatchCount, before.skippedDispatchCount);
        gShadowRendererState.debugStats.shadowMeshletSubmitCallCount +=
            StatDelta(after.submitCallCount, before.submitCallCount);
        gShadowRendererState.debugStats.shadowMeshletSkippedBucketCount +=
            StatDelta(after.skippedBucketCount, before.skippedBucketCount);
        gShadowRendererState.debugStats.shadowMeshletBackFaceSubmitCallCount +=
            StatDelta(after.backFaceSubmitCallCount, before.backFaceSubmitCallCount);
        gShadowRendererState.debugStats.shadowMeshletDoubleSidedSubmitCallCount +=
            StatDelta(after.doubleSidedSubmitCallCount, before.doubleSidedSubmitCallCount);
        gShadowRendererState.debugStats.shadowMeshletPipelineReady = after.shadowPipelineReady;
        gShadowRendererState.debugStats.shadowMeshletDispatchArgumentBufferReady =
            after.dispatchArgumentBufferReady;
        gShadowRendererState.debugStats.shadowMeshletDispatchCommandSignatureReady =
            after.dispatchCommandSignatureReady;
    }

    void MarkShadowCacheHit() {
        gShadowRendererState.shadowCache.MarkHit();
        PublishShadowCacheStats();
    }

    void MarkShadowCacheMiss() {
        gShadowRendererState.shadowCache.MarkMiss();
        PublishShadowCacheStats();
    }

    void MarkShadowCacheValidAfterRender() {
        ShadowCacheKey key{};
        key.layoutVersion = gShadowRendererState.staticShadowSceneSource.layoutVersion;
        key.sourceVersion = gShadowRendererState.staticShadowSceneSource.sourceVersion;
        key.sourceInstanceCount =
            gShadowRendererState.staticShadowSceneSource.sourceInstanceCount;
        key.resolution = gShadowRendererState.resolution;
        key.lightViewProj = gShadowRendererState.lightViewProj;
        gShadowRendererState.shadowCache.MarkValid(key);
        PublishShadowCacheStats();
    }

} // namespace HIKARI::SHADOW::INTERNAL
