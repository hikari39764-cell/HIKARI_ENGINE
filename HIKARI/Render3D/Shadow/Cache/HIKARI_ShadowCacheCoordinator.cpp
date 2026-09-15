#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <algorithm>

namespace HIKARI::SHADOW::INTERNAL {

    void InvalidateShadowSourceCache(ShadowRendererState& state) {
        state.shadowSourceCache.Invalidate();
        state.shadowPrimaryLocations.clear();
    }

    bool CanReuseShadowSourceCache(ShadowRendererState& state) {
        return state.shadowSourceCache.Matches(state.gpuDrivenSceneSource);
    }

    void PublishShadowCacheStats(ShadowRendererState& state) {
        state.shadowCache.PublishStats(state.debugStats);
    }

    void InvalidateShadowCache(ShadowRendererState& state) {
        state.shadowCache.Invalidate();
        PublishShadowCacheStats(state);
    }

    bool HasStaticShadowDirtyRanges(ShadowRendererState& state) {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
            state.staticShadowSceneSource.GetPass(
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

    bool CanReuseShadowCache(ShadowRendererState& state, uint32_t resolution) {
        ShadowCacheReuseInput input{};
        input.key.layoutVersion = state.staticShadowSceneSource.layoutVersion;
        input.key.sourceVersion = state.staticShadowSceneSource.sourceVersion;
        input.key.sourceInstanceCount =
            state.staticShadowSceneSource.sourceInstanceCount;
        input.key.resolution = resolution;
        input.key.lightViewProj =
            state.cascadeLightViewProj;
        input.key.cascadeCount = state.cascadeCount;
        input.hasStaticWork = state.frameHasStaticShadowWork;
        input.resourcesReady =
            state.staticShadowMap != nullptr &&
            state.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(state.shadowSrvResource);
        input.stateReusable =
            state.staticShadowState == D3D12_RESOURCE_STATE_COPY_SOURCE ||
            state.staticShadowState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        input.hasStaticDirtyRanges = HasStaticShadowDirtyRanges(state);
        return state.shadowCache.EvaluateReuse(input);
    }

    size_t StatDelta(size_t after, size_t before) {
        return after >= before ? after - before : after;
    }

    void AccumulateShadowMeshletStatsDelta(
        ShadowRendererState& state, const RENDER3D::MESHLET::MeshletRenderBackendStats& before,
        const RENDER3D::MESHLET::MeshletRenderBackendStats& after) {

        state.debugStats.shadowMeshletRequestedDispatchCount +=
            StatDelta(after.requestedDispatchCount, before.requestedDispatchCount);
        state.debugStats.shadowMeshletSubmittedDispatchCount +=
            StatDelta(after.shadowSubmittedDispatchCount, before.shadowSubmittedDispatchCount);
        state.debugStats.shadowMeshletSkippedDispatchCount +=
            StatDelta(after.skippedDispatchCount, before.skippedDispatchCount);
        state.debugStats.shadowMeshletSubmitCallCount +=
            StatDelta(after.submitCallCount, before.submitCallCount);
        state.debugStats.shadowMeshletSkippedBucketCount +=
            StatDelta(after.skippedBucketCount, before.skippedBucketCount);
        state.debugStats.shadowMeshletBackFaceSubmitCallCount +=
            StatDelta(after.backFaceSubmitCallCount, before.backFaceSubmitCallCount);
        state.debugStats.shadowMeshletDoubleSidedSubmitCallCount +=
            StatDelta(after.doubleSidedSubmitCallCount, before.doubleSidedSubmitCallCount);
        state.debugStats.shadowMeshletPipelineReady = after.shadowPipelineReady;
        state.debugStats.shadowMeshletDispatchArgumentBufferReady =
            after.dispatchArgumentBufferReady;
        state.debugStats.shadowMeshletDispatchCommandSignatureReady =
            after.dispatchCommandSignatureReady;
    }

    void MarkShadowCacheHit(ShadowRendererState& state) {
        state.shadowCache.MarkHit();
        PublishShadowCacheStats(state);
    }

    void MarkShadowCacheMiss(ShadowRendererState& state) {
        state.shadowCache.MarkMiss();
        PublishShadowCacheStats(state);
    }

    void MarkShadowCacheValidAfterRender(ShadowRendererState& state) {
        ShadowCacheKey key{};
        key.layoutVersion = state.staticShadowSceneSource.layoutVersion;
        key.sourceVersion = state.staticShadowSceneSource.sourceVersion;
        key.sourceInstanceCount =
            state.staticShadowSceneSource.sourceInstanceCount;
        key.resolution = state.resolution;
        key.lightViewProj = state.cascadeLightViewProj;
        key.cascadeCount = state.cascadeCount;
        state.shadowCache.MarkValid(key);
        PublishShadowCacheStats(state);
    }

} // namespace HIKARI::SHADOW::INTERNAL
