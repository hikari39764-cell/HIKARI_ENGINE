#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "HIKARI_Services.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    bool ExecuteMeshletDrawFrame(
        const MeshPassResources& passResources,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind pipelineKind) {
        if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
            return false;
        }

        const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
            gMeshRendererState.gpuDrivenLayer.BuildGeometryBackendContext(
                SERVICES::gCtx.cmdList,
                gpuPass,
                RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader);
        ID3D12Resource* visibleRangeBuffer =
            backendContext.visibility != nullptr
                ? backendContext.visibility->visibleMeshletRangeBuffer
                : nullptr;
        ID3D12Resource* visibleClusterListBuffer =
            backendContext.visibility != nullptr
                ? backendContext.visibility->visibleMeshletClusterListBuffer
                : nullptr;
        if (visibleRangeBuffer == nullptr ||
            visibleClusterListBuffer == nullptr) {
            return false;
        }

        MeshBindingStateCache bindingCache{};
        const bool depthAwarePhase =
            gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
        MeshDrawContext drawCtx = BuildDrawContext(depthAwarePhase, passKind, passResources);
        drawCtx.binding.cache = &bindingCache;
        BindSurfaceRecordFrameResources(drawCtx);
        BindMeshletVisibleRanges(
            drawCtx.binding,
            visibleRangeBuffer->GetGPUVirtualAddress());
        BindMeshletVisibleClusterList(
            drawCtx.binding,
            visibleClusterListBuffer->GetGPUVirtualAddress());
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        BindMeshletDeformationPalettes(
            drawCtx.binding,
            frame.jointPaletteCB != nullptr
                ? frame.jointPaletteCB->GetGPUVirtualAddress()
                : 0u);

        RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
        ctx.commandList = backendContext.commandList;
        ctx.pass = backendContext.pass;
        ctx.visibility = backendContext.visibility;
        ctx.drawCommandRange = backendContext.drawCommandRange;
        ctx.pipelineKind = pipelineKind;
        const bool executed = gMeshRendererState.meshletRenderBackend.Execute(ctx);
        UpdateMeshletBackendDebugStats();
        UpdateGpuDrivenWorkReadyDebugStats();
        return executed;
    }

    ID3D12PipelineState* ResolveTraditionalStaticPso(
        MeshDrawPassKind passKind,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const VFX::VariantKey& bucketVariant) {

        (void)gpuPass;

        if (passKind == MeshDrawPassKind::GeometryAux) {
            return gMeshRendererState.pipelines.geometryPso.Get();
        }
        if (passKind == MeshDrawPassKind::DepthPrepass) {
            return gMeshRendererState.pipelines.depthPso.Get();
        }
        if (passKind != MeshDrawPassKind::Forward ||
            SERVICES::gCtx.device == nullptr) {
            return gMeshRendererState.pipelines.pso.Get();
        }

        return GetOrCreateVariantPso(
            gMeshRendererState.pipelines,
            SERVICES::gCtx.device,
            gMeshRendererState.debugStats,
            bucketVariant,
            false,
            false);
    }

    ID3D12PipelineState* ResolveTraditionalSkinnedPso(
        MeshDrawPassKind passKind,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const VFX::VariantKey& bucketVariant) {

        (void)gpuPass;

        if (!bucketVariant.vertexShaderId.empty() &&
            bucketVariant.vertexShaderId != "Render3D_StaticVS") {
            return nullptr;
        }
        if (passKind == MeshDrawPassKind::GeometryAux) {
            return gMeshRendererState.pipelines.geometrySkinnedPso.Get();
        }
        if (passKind == MeshDrawPassKind::DepthPrepass) {
            return gMeshRendererState.pipelines.depthSkinnedPso.Get();
        }
        if (passKind != MeshDrawPassKind::Forward ||
            SERVICES::gCtx.device == nullptr) {
            return gMeshRendererState.pipelines.skinnedPso.Get();
        }

        VFX::VariantKey key = bucketVariant;
        key.vertexShaderId.clear();
        return GetOrCreateVariantPso(
            gMeshRendererState.pipelines,
            SERVICES::gCtx.device,
            gMeshRendererState.debugStats,
            key,
            true,
            false);
    }

    bool ExecuteTraditionalDrawFrame(
        const MeshPassResources& passResources,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        MeshDrawPassKind passKind
#if defined(HIKARI_WITH_EDITOR)
        ,
        RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride,
        const MeshFrameBindingOverrides* bindingOverrides
#endif
    ) {

#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
            layerOverride != nullptr ? *layerOverride : gMeshRendererState.gpuDrivenLayer;
        if (layerOverride == nullptr) {
            SyncGpuDrivenBackendAvailability();
        }
        if (!layer.IsPassGpuReady(gpuPass)) {
            return false;
        }
#else
        if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
            return false;
        }
#endif

        const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
#if defined(HIKARI_WITH_EDITOR)
            layer.BuildGeometryBackendContext(
#else
            gMeshRendererState.gpuDrivenLayer.BuildGeometryBackendContext(
#endif
                SERVICES::gCtx.cmdList,
                gpuPass,
                RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs);
        const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandRange* range =
            backendContext.drawCommandRange;
        if (range == nullptr ||
            !range->gpuAuthored ||
            !range->gpuCounterBacked ||
            range->counterBuffer == nullptr ||
            range->commandCount == 0) {
            return false;
        }
        const size_t commandBucketCapacity = range->commandBucketCapacity;
        const size_t commandBucketCount = range->commandBucketCount;
        const bool staticBucketLayoutReady =
            commandBucketCapacity != 0 &&
            commandBucketCount != 0 &&
            range->argumentBucketStride != 0 &&
            range->counterBucketStride != 0;
        const bool skinnedBucketLayoutReady =
            commandBucketCapacity != 0 &&
            commandBucketCount != 0 &&
            range->skinnedArgumentBucketStride != 0 &&
            range->counterBucketStride != 0;
        const bool hasStaticStream =
            range->argumentBuffer != nullptr &&
            range->commandSignature != nullptr &&
            staticBucketLayoutReady &&
            range->staticCommandCount != 0;
        const bool hasSkinnedStream =
            range->skinnedArgumentBuffer != nullptr &&
            range->skinnedCommandSignature != nullptr &&
            skinnedBucketLayoutReady &&
            range->skinnedCommandCount != 0;
        if (!hasStaticStream && !hasSkinnedStream) {
            return false;
        }
        const std::vector<VFX::VariantKey>* bucketVariants =
            range->traditionalIndirect != nullptr
                ? range->traditionalIndirect->bucketVariants
                : nullptr;
        if (bucketVariants == nullptr || bucketVariants->empty()) {
            return false;
        }
        const size_t executableBucketCount =
            (std::min)(commandBucketCount, bucketVariants->size());
        if (executableBucketCount == 0) {
            return false;
        }

        MeshBindingStateCache bindingCache{};
        const bool depthAwarePhase =
            gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
        MeshDrawContext drawCtx =
            BuildDrawContext(
                depthAwarePhase,
                passKind,
                passResources
#if defined(HIKARI_WITH_EDITOR)
                ,
                bindingOverrides);
#else
            );
#endif
        drawCtx.binding.cache = &bindingCache;
        drawCtx.surfaceGpuSceneBaseOffset = range->gpuSceneBaseIndex;
        BindSurfaceRecordFrameResources(drawCtx);
        BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
        BindObjectDataIndex(drawCtx.binding, 0u);
        BindMaterialDataIndex(drawCtx.binding, 0u);
        const size_t uintMaxCommandCount =
            static_cast<size_t>((std::numeric_limits<UINT>::max)());
        const size_t staticCommandLimit =
            (std::min)(range->staticCommandCount, commandBucketCapacity);
        const size_t skinnedCommandLimit =
            (std::min)(range->skinnedCommandCount, commandBucketCapacity);
        const UINT maxStaticCommandCount = static_cast<UINT>(
            (std::min)(staticCommandLimit, uintMaxCommandCount));
        const UINT maxSkinnedCommandCount = static_cast<UINT>(
            (std::min)(skinnedCommandLimit, uintMaxCommandCount));
        bool executed = false;
        if (hasStaticStream) {
            for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                const VFX::VariantKey& bucketVariant =
                    (*bucketVariants)[bucketIndex];
                ID3D12PipelineState* bucketPso =
                    ResolveTraditionalStaticPso(
                        passKind,
                        gpuPass,
                        bucketVariant);
                if (bucketPso == nullptr) {
                    continue;
                }
                BindPipelineState(drawCtx.binding, bucketPso);
                SERVICES::gCtx.cmdList->ExecuteIndirect(
                    range->commandSignature,
                    maxStaticCommandCount,
                    range->argumentBuffer,
                    range->argumentBufferOffset +
                        static_cast<UINT64>(bucketIndex) *
                        range->argumentBucketStride,
                    range->counterBuffer,
                    range->counterBufferOffset +
                        static_cast<UINT64>(bucketIndex) *
                        range->counterBucketStride);
                executed = true;
            }
        }

        if (hasSkinnedStream &&
            drawCtx.skinnedRootSig != nullptr) {
            BindFrameCommonResources(
                drawCtx.binding,
                drawCtx.skinnedRootSig,
                drawCtx.cameraAddress,
                drawCtx.cullingCameraAddress,
                drawCtx.lightAddress,
                drawCtx.shadowAddress,
                drawCtx.skyEnvironmentAddress);
            BindObjectDataBuffer(drawCtx.binding, drawCtx.objectDataSrv);
            BindMaterialDataBuffer(drawCtx.binding, drawCtx.materialDataSrv);
            BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
            BindSurfaceGpuSceneControl(drawCtx.binding, 0u, false);
            BindShadowMap(drawCtx.binding);
            if (passKind == MeshDrawPassKind::Forward) {
                BindSkyCube(drawCtx.binding);
                BindSceneDepth(drawCtx.binding);
                BindSceneColor(drawCtx.binding);
                BindIblResources(drawCtx.binding);
                BindReflectionProbeResources(drawCtx.binding);
                BindSsao(drawCtx.binding);
                BindLightProbeResources(drawCtx.binding);
            }
            BindObjectDataIndex(drawCtx.binding, 0u);
            BindMaterialDataIndex(drawCtx.binding, 0u);
            for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                const VFX::VariantKey& bucketVariant =
                    (*bucketVariants)[bucketIndex];
                ID3D12PipelineState* bucketPso =
                    ResolveTraditionalSkinnedPso(
                        passKind,
                        gpuPass,
                        bucketVariant);
                if (bucketPso == nullptr) {
                    continue;
                }
                BindPipelineState(drawCtx.binding, bucketPso);
                SERVICES::gCtx.cmdList->ExecuteIndirect(
                    range->skinnedCommandSignature,
                    maxSkinnedCommandCount,
                    range->skinnedArgumentBuffer,
                    range->skinnedArgumentBufferOffset +
                        static_cast<UINT64>(bucketIndex) *
                        range->skinnedArgumentBucketStride,
                    range->counterBuffer,
                    range->skinnedCounterBufferOffset +
                        static_cast<UINT64>(bucketIndex) *
                        range->counterBucketStride);
                executed = true;
            }
        }

        gMeshRendererState.debugStats.gpuDrivenSkinnedCommandCount +=
            range->skinnedCommandCount;
        gMeshRendererState.debugStats.gpuDrivenSkinnedSourceRecordCount +=
            range->skinnedCommandCount;
        return executed;
    }

    bool ExecuteGeometryBackend(
        RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const MeshPassResources& passResources,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind) {

        switch (backend) {
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader:
            return ExecuteMeshletDrawFrame(
                passResources,
                gpuPass,
                passKind,
                meshletPipelineKind);
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs:
            return ExecuteTraditionalDrawFrame(
                passResources,
                gpuPass,
                passKind);
        default:
            return false;
        }
    }

    GeometryBackendExecutionResult ExecuteGeometryBackendPlan(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
        const MeshPassResources& passResources,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind) {

        GeometryBackendExecutionResult result{};
        SyncGpuDrivenBackendAvailability();
        const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionPlan(pass);
        for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
            const RENDER3D::GPUDRIVEN::GeometryBackendKind backend =
                plan.gpuBackends[i];
            if (!ExecuteGeometryBackend(
                backend,
                pass,
                passResources,
                passKind,
                meshletPipelineKind)) {
                continue;
            }
            result.gpuBackendExecuted = true;
            result.executedGpuBackend = backend;
        }
        return result;
    }

    GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
        const MeshPassResources& passResources) {

        GeometryBackendExecutionResult result{};
        SyncGpuDrivenBackendAvailability();
        const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionPlan(pass);
        for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
            const RENDER3D::GPUDRIVEN::GeometryBackendKind backend =
                plan.gpuBackends[i];
            if (!ExecuteGeometryBackend(
                backend,
                pass,
                passResources,
                MeshDrawPassKind::DepthPrepass,
                RENDER3D::MESHLET::MeshletPipelineKind::DepthPrepass)) {
                continue;
            }
            result.gpuBackendExecuted = true;
            result.executedGpuBackend = backend;
        }
        return result;
    }


} // namespace HIKARI::MESHRENDERER::INTERNAL
