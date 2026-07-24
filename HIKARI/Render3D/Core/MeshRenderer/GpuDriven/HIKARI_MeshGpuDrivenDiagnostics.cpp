#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    void UpdateMeshletBackendDebugStats() {
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        const RENDER3D::GPUDRIVEN::GpuCommandBuildResult& gpuDrivenCommands =
            gMeshRendererState.gpuDrivenLayer.GetFrameContext().commands;
        gMeshRendererState.debugStats.meshletBackendInitialized =
            meshletStats.initialized;
        gMeshRendererState.debugStats.meshletBackendShaderModel65Supported =
            meshletStats.shaderModel65Supported;
        gMeshRendererState.debugStats.meshletBackendMeshShaderSupported =
            meshletStats.meshShaderSupported;
        gMeshRendererState.debugStats.meshletBackendPipelineStatsSupported =
            meshletStats.meshShaderPipelineStatsSupported;
        gMeshRendererState.debugStats.meshletBackendShaderCompileReady =
            meshletStats.shaderCompileReady;
        gMeshRendererState.debugStats.meshletBackendDispatchArgumentBufferReady =
            meshletStats.dispatchArgumentBufferReady ||
            gpuDrivenCommands.meshDispatchArgs != nullptr;
        gMeshRendererState.debugStats.meshletBackendDispatchCommandSignatureReady =
            meshletStats.dispatchCommandSignatureReady ||
            gpuDrivenCommands.meshDispatchSignature != nullptr;
        gMeshRendererState.debugStats.meshletBackendForwardPipelineReady =
            meshletStats.forwardPipelineReady;
        gMeshRendererState.debugStats.meshletBackendGeometryAuxPipelineReady =
            meshletStats.geometryAuxPipelineReady;
        gMeshRendererState.debugStats.meshletBackendPipelineReady =
            meshletStats.pipelineReady;
        gMeshRendererState.debugStats.meshletBackendMeshShaderTier =
            meshletStats.meshShaderTier;
        gMeshRendererState.debugStats.meshletBackendRequestedDispatchCount =
            meshletStats.requestedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendSubmittedDispatchCount =
            meshletStats.submittedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendSkippedDispatchCount =
            meshletStats.skippedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendSubmitCallCount =
            meshletStats.submitCallCount;
        gMeshRendererState.debugStats.meshletBackendSkippedBucketCount =
            meshletStats.skippedBucketCount;
        gMeshRendererState.debugStats.meshletBackendForwardSubmittedDispatchCount =
            meshletStats.forwardSubmittedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendGeometryAuxSubmittedDispatchCount =
            meshletStats.geometryAuxSubmittedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendDepthPrepassSubmittedDispatchCount =
            meshletStats.depthPrepassSubmittedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendShadowSubmittedDispatchCount =
            meshletStats.shadowSubmittedDispatchCount;
        gMeshRendererState.debugStats.meshletBackendBackFaceSubmitCallCount =
            meshletStats.backFaceSubmitCallCount;
        gMeshRendererState.debugStats.meshletBackendDoubleSidedSubmitCallCount =
            meshletStats.doubleSidedSubmitCallCount;
        gMeshRendererState.debugStats.meshletBackendPipelineCreateRequestCount =
            meshletStats.pipelineCreateRequestCount;
        gMeshRendererState.debugStats.meshletBackendPipelineCreateReadyCount =
            meshletStats.pipelineCreateReadyCount;
    }

    void SyncGpuDrivenBackendAvailability() {
        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();

        availability.meshShaderForwardPipelineReady =
            meshletStats.forwardPipelineReady &&
            meshletStats.depthAwarePipelineReady &&
            meshletStats.transparentPipelineReady;
        availability.meshShaderGeometryAuxPipelineReady =
            meshletStats.geometryAuxPipelineReady;
        availability.traditionalIndirectPipelineReady =
            GetStaticRootSignature(gMeshRendererState.pipelines) != nullptr &&
            GetSkinnedRootSignature(gMeshRendererState.pipelines) != nullptr &&
            gMeshRendererState.pipelines.pso != nullptr &&
            gMeshRendererState.pipelines.skinnedPso != nullptr &&
            gMeshRendererState.pipelines.depthPso != nullptr &&
            gMeshRendererState.pipelines.depthSkinnedPso != nullptr;
        gMeshRendererState.gpuDrivenLayer.SetBackendAvailability(availability);
    }

    void UpdateGpuDrivenWorkReadyDebugStats() {
        SyncGpuDrivenBackendAvailability();
        const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& forward =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
        const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& geometry =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
        gMeshRendererState.debugStats.clusterMainlineReady = forward.gpuBackendReady;
        gMeshRendererState.debugStats.clusterMainlineForwardReady = forward.gpuBackendReady;
        gMeshRendererState.debugStats.clusterMainlineGeometryAuxReady = geometry.gpuBackendReady;
        gMeshRendererState.debugStats.clusterMainlineHasDrawSeeds =
            forward.hasDrawSeeds ||
            geometry.hasDrawSeeds;
        gMeshRendererState.debugStats.clusterMainlineOverflowBlocked =
            forward.overflowBlocked ||
            geometry.overflowBlocked;
        UpdateGpuDrivenCommandStreamDebugStats();
    }

    void UpdateGpuDrivenCommandStreamDebugStats() {
        const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandStream& stream =
            gMeshRendererState.gpuDrivenLayer.GetDrawCommandStream();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamPassCount =
            stream.CountActivePasses();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamRangeCount =
            stream.CountActiveRanges();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamGpuCommandCount =
            stream.CountGpuAuthoredCommands();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamTraditionalCommandCount =
            stream.CountTraditionalIndirectCommands();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamGpuCounterBackedRangeCount =
            stream.CountGpuCounterBackedRanges();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamKnownVisibleCommandCount =
            stream.CountKnownGpuVisibleCommands();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamKnownVisibleCommandOverflowCount =
            stream.CountKnownGpuVisibleCommandOverflows();
    }

    void ApplyGpuDrivenWorkOwnershipDebugStats(
        const RENDER3D::GPUDRIVEN::GpuDrivenWorkOwnershipStats& stats) {

        gMeshRendererState.debugStats.clusterMainlineOwnedCommandCount = stats.ownedCommandCount;
        gMeshRendererState.debugStats.clusterMainlineOwnedRecordCount = stats.ownedRecordCount;
        gMeshRendererState.debugStats.clusterMainlineGeometryAuxCommandCount = stats.geometryAuxCommandCount;
        gMeshRendererState.debugStats.clusterMainlineGeometryAuxRecordCount = stats.geometryAuxRecordCount;
    }

    void UpdateGpuDrivenWorkOwnershipDebugStats() {
        SyncGpuDrivenBackendAvailability();
        RENDER3D::GPUDRIVEN::GpuDrivenWorkOwnershipStats stats{};
        const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& forward =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
        const RENDER3D::GPUDRIVEN::GpuDrivenPassExecutionState& geometry =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
        if (forward.gpuBackendReady) {
            stats.ownedCommandCount = forward.sourceInstanceCount;
            stats.ownedRecordCount = forward.sourceInstanceCount;
            stats.eligibleCommandCount = stats.ownedCommandCount;
        }
        if (geometry.gpuBackendReady) {
            stats.geometryAuxCommandCount = geometry.sourceInstanceCount;
            stats.geometryAuxRecordCount = geometry.sourceInstanceCount;
        }
        ApplyGpuDrivenWorkOwnershipDebugStats(stats);
    }

    bool IsGpuDrivenWorkPreparedForPass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass) {

        SyncGpuDrivenBackendAvailability();
        return gMeshRendererState.gpuDrivenLayer.IsPassGpuReady(pass);
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
