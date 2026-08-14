#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    bool ExecuteMeshletDrawFrame(
        const GFX::Context& graphics,
        const MeshPassResources& passResources,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind pipelineKind
#if defined(HIKARI_WITH_EDITOR)
        ,
        RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
        const MeshFrameBindingOverrides* bindingOverrides = nullptr
#endif
    ) {
#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
            layerOverride != nullptr
                ? *layerOverride
                : gMeshRendererState.gpuDrivenLayer;
        if (layerOverride != nullptr) {
            if (!layer.IsPassGpuReady(gpuPass)) {
                return false;
            }
        } else if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
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
                graphics.cmdList,
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
        MeshDrawContext drawCtx =
            BuildDrawContext(
                graphics,
                depthAwarePhase,
                passKind,
                passResources
#if defined(HIKARI_WITH_EDITOR)
                ,
                bindingOverrides
#endif
            );
        drawCtx.binding.cache = &bindingCache;
        BindSurfaceRecordFrameResources(drawCtx);
        BindMeshletVisibleRanges(
            drawCtx.binding,
            visibleRangeBuffer->GetGPUVirtualAddress());
        BindMeshletVisibleClusterList(
            drawCtx.binding,
            visibleClusterListBuffer->GetGPUVirtualAddress());
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        D3D12_GPU_VIRTUAL_ADDRESS jointPaletteAddress =
            frame.jointPaletteCB != nullptr
                ? frame.jointPaletteCB->GetGPUVirtualAddress()
                : 0u;
#if defined(HIKARI_WITH_EDITOR)
        if (bindingOverrides != nullptr &&
            bindingOverrides->jointPaletteAddress != 0u) {
            jointPaletteAddress = bindingOverrides->jointPaletteAddress;
        }
#endif
        BindMeshletDeformationPalettes(
            drawCtx.binding,
            jointPaletteAddress);

        RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
        ctx.commandList = backendContext.commandList;
        ctx.pass = backendContext.pass;
        ctx.visibility = backendContext.visibility;
        ctx.drawCommandRange = backendContext.drawCommandRange;
        ctx.pipelineKind = pipelineKind;
        const bool executed = gMeshRendererState.meshletRenderBackend.Execute(ctx);
        UpdateMeshletBackendDebugStats();
        return executed;
    }

    ID3D12PipelineState* ResolveTraditionalStaticPso(
        const GFX::Context& graphics,
        MeshDrawPassKind passKind,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const VFX::VariantKey& bucketVariant) {

        (void)gpuPass;

        if (passKind == MeshDrawPassKind::GeometryAux) {
            return gMeshRendererState.pipelines.geometryPso.Get();
        }
        if (passKind == MeshDrawPassKind::GeometryAuxFeatureMask) {
            return gMeshRendererState.pipelines.geometryFeatureMaskPso.Get();
        }
        if (passKind == MeshDrawPassKind::GeometryOutline) {
            return gMeshRendererState.pipelines.geometryOutlinePso.Get();
        }
        if (passKind == MeshDrawPassKind::VisibilityOverlay) {
            return gMeshRendererState.pipelines.visibilityOverlayPso.Get();
        }
        if (passKind == MeshDrawPassKind::DepthPrepass) {
            return gMeshRendererState.pipelines.depthPso.Get();
        }
        if (passKind != MeshDrawPassKind::Forward ||
            graphics.device == nullptr) {
            return gMeshRendererState.pipelines.pso.Get();
        }

        return GetOrCreateVariantPso(
            gMeshRendererState.pipelines,
            graphics.device,
            gMeshRendererState.debugStats,
            bucketVariant,
            false,
            false);
    }

    ID3D12PipelineState* ResolveTraditionalSkinnedPso(
        const GFX::Context& graphics,
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
        if (passKind == MeshDrawPassKind::GeometryAuxFeatureMask) {
            return gMeshRendererState.pipelines.
                geometryFeatureMaskSkinnedPso.Get();
        }
        if (passKind == MeshDrawPassKind::GeometryOutline) {
            return gMeshRendererState.pipelines.geometryOutlineSkinnedPso.Get();
        }
        if (passKind == MeshDrawPassKind::VisibilityOverlay) {
            return gMeshRendererState.pipelines.visibilityOverlaySkinnedPso.Get();
        }
        if (passKind == MeshDrawPassKind::DepthPrepass) {
            return gMeshRendererState.pipelines.depthSkinnedPso.Get();
        }
        if (passKind != MeshDrawPassKind::Forward ||
            graphics.device == nullptr) {
            return gMeshRendererState.pipelines.skinnedPso.Get();
        }

        VFX::VariantKey key = bucketVariant;
        key.vertexShaderId.clear();
        return GetOrCreateVariantPso(
            gMeshRendererState.pipelines,
            graphics.device,
            gMeshRendererState.debugStats,
            key,
            true,
            false);
    }

    bool ExecuteTraditionalDrawFrame(
        const GFX::Context& graphics,
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
                graphics.cmdList,
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
                graphics,
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
                        graphics,
                        passKind,
                        gpuPass,
                        bucketVariant);
                if (bucketPso == nullptr) {
                    continue;
                }
                BindPipelineState(drawCtx.binding, bucketPso);
                graphics.cmdList->ExecuteIndirect(
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
                drawCtx.localLightsAddress,
                drawCtx.localLightClusterHeadersAddress,
                drawCtx.localLightClusterIndicesAddress,
                drawCtx.materialProgramParametersAddress,
                drawCtx.materialProgramResourcesAddress,
                drawCtx.shadowAddress,
                drawCtx.skyEnvironmentAddress);
            BindObjectDataBuffer(drawCtx.binding, drawCtx.objectDataSrv);
            BindMaterialDataBuffer(drawCtx.binding, drawCtx.materialDataSrv);
            BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
            BindSurfaceGpuSceneControl(drawCtx.binding, 0u, false);
            BindSurfaceFeatureData(
                drawCtx.binding,
                drawCtx.surfaceFeatureDataAddress);
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
                        graphics,
                        passKind,
                        gpuPass,
                        bucketVariant);
                if (bucketPso == nullptr) {
                    continue;
                }
                BindPipelineState(drawCtx.binding, bucketPso);
                graphics.cmdList->ExecuteIndirect(
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
        const GFX::Context& graphics,
        RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const MeshPassResources& passResources,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind
#if defined(HIKARI_WITH_EDITOR)
        ,
        RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
        const MeshFrameBindingOverrides* bindingOverrides = nullptr
#endif
    ) {

        switch (backend) {
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader:
            return ExecuteMeshletDrawFrame(
                graphics,
                passResources,
                gpuPass,
                passKind,
                meshletPipelineKind
#if defined(HIKARI_WITH_EDITOR)
                ,
                layerOverride,
                bindingOverrides
#endif
            );
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs:
            return ExecuteTraditionalDrawFrame(
                graphics,
                passResources,
                gpuPass,
                passKind
#if defined(HIKARI_WITH_EDITOR)
                ,
                layerOverride,
                bindingOverrides
#endif
            );
        default:
            return false;
        }
    }

    GeometryBackendExecutionResult ExecuteGeometryBackendPlan(
        const GFX::Context& graphics,
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
                graphics,
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

#if defined(HIKARI_WITH_EDITOR)
    GeometryBackendExecutionResult ExecuteEditorInteractiveGeometryBackendPlan(
        const GFX::Context& graphics,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
        const MeshPassResources& passResources,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
        const MeshFrameBindingOverrides& bindingOverrides) {

        GeometryBackendExecutionResult result{};
        const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
            layer.GetPassExecutionPlan(pass);
        for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
            const RENDER3D::GPUDRIVEN::GeometryBackendKind backend =
                plan.gpuBackends[i];
            if (!ExecuteGeometryBackend(
                    graphics,
                    backend,
                    pass,
                    passResources,
                    passKind,
                    meshletPipelineKind,
                    &layer,
                    &bindingOverrides)) {
                continue;
            }
            result.gpuBackendExecuted = true;
            result.executedGpuBackend = backend;
        }
        return result;
    }
#endif

    GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(
        const GFX::Context& graphics,
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
                graphics,
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
