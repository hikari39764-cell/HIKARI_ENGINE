#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    void UpdateMeshletBackendDebugStats() {
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        const RENDER3D::GPUDRIVEN::GpuCommandBuildResult& gpuDrivenCommands =
            gMeshRendererState.gpuDrivenLayer.GetFrameContext().commands;
        gMeshRendererState.debugStats.meshletBackend = meshletStats;
        gMeshRendererState.debugStats.meshletDispatchArgumentBufferReady =
            meshletStats.dispatchArgumentBufferReady ||
            gpuDrivenCommands.meshDispatchArgs != nullptr;
        gMeshRendererState.debugStats.meshletDispatchCommandSignatureReady =
            meshletStats.dispatchCommandSignatureReady ||
            gpuDrivenCommands.meshDispatchSignature != nullptr;
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
            AreTraditionalMeshPipelineInputsReady(gMeshRendererState.pipelines);
        gMeshRendererState.gpuDrivenLayer.SetBackendAvailability(availability);
    }

    void UpdateGpuDrivenCommandStreamDebugStats() {
        const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandStream& stream =
            gMeshRendererState.gpuDrivenLayer.GetDrawCommandStream();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamGpuCommandCount =
            stream.CountGpuAuthoredCommands();
        gMeshRendererState.debugStats.gpuDrivenCommandStreamTraditionalCommandCount =
            stream.CountTraditionalIndirectCommands();
    }

    bool IsGpuDrivenWorkPreparedForPass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass) {

        SyncGpuDrivenBackendAvailability();
        return gMeshRendererState.gpuDrivenLayer.IsPassGpuReady(pass);
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
