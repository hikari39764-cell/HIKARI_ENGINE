#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
bool ExecuteMeshletDrawFrame(const GFX::Context& graphics, const MeshPassResources& passResources,
                             RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass, MeshDrawPassKind passKind,
                             RENDER3D::MESHLET::MeshletPipelineKind pipelineKind,
                             RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
                             const MeshFrameBindingOverrides* bindingOverrides = nullptr) {
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
        layerOverride != nullptr ? *layerOverride : gMeshRendererState.gpuDrivenLayer;
    if (layerOverride != nullptr) {
        if (!layer.IsPassGpuReady(gpuPass)) {
            return false;
        }
    } else if (!IsGpuDrivenWorkPreparedForPass(gpuPass)) {
        return false;
    }

    const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext = layer.BuildGeometryBackendContext(
        graphics.cmdList, gpuPass, RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader);
    ID3D12Resource* visibleRangeBuffer =
        backendContext.visibility != nullptr ? backendContext.visibility->visibleMeshletRangeBuffer : nullptr;
    if (visibleRangeBuffer == nullptr) {
        return false;
    }

    MeshBindingStateCache bindingCache{};
    const bool depthAwarePhase = gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
    MeshDrawContext drawCtx = BuildDrawContext(graphics, depthAwarePhase, passKind, passResources, bindingOverrides);
    drawCtx.binding.cache = &bindingCache;
    BindSurfaceRecordFrameResources(drawCtx);
    BindMeshletVisibleRanges(drawCtx.binding, visibleRangeBuffer->GetGPUVirtualAddress());
    const MeshFrameResources& frame = GetActiveMeshFrameResources();
    D3D12_GPU_VIRTUAL_ADDRESS jointPaletteAddress = frame.jointPaletteAddress;
    if (bindingOverrides != nullptr && bindingOverrides->jointPaletteAddress != 0u) {
        jointPaletteAddress = bindingOverrides->jointPaletteAddress;
    }
    BindMeshletDeformationPalettes(drawCtx.binding, jointPaletteAddress);

    const auto& visibilityDepth = passResources.visibilityDepthPyramid;
    const bool occlusionValid = gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque &&
                                // SSR 用 MRT は同じ視点の主描画。capture 出力形式で可視性を無効にしない。
                                passKind == MeshDrawPassKind::Forward && GetCameraConstants() != nullptr &&
                                GetGpuDrivenCullingCameraConstants() != nullptr &&
                                RENDER3D::MESHLET::IsMeshletOcclusionViewUsable(
                                    visibilityDepth, GetCameraConstants()->viewProj,
                                    GetGpuDrivenCullingCameraConstants()->viewProj, IsGpuDrivenCullingCameraFrozen());
    const bool aux = passKind == MeshDrawPassKind::GeometryAux || passKind == MeshDrawPassKind::GeometryAuxFeatureMask;
    const uint32_t visibilityPhase =
        aux ? 0u
            : (passKind == MeshDrawPassKind::Forward && gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque
                   ? 1u
                   : 2u);
    const auto visibilityBinding = gMeshRendererState.meshletRenderBackend.BeginVisibility(
        graphics, occlusionValid ? &visibilityDepth : nullptr, visibilityPhase);
    if (visibilityBinding.table.ptr == 0u)
        return false;
    graphics.cmdList->SetGraphicsRootDescriptorTable(ROOT_PARAM::MeshletOcclusionDepth, visibilityBinding.table);

    RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
    ctx.commandList = backendContext.commandList;
    ctx.pass = backendContext.pass;
    ctx.visibility = backendContext.visibility;
    ctx.drawCommandRange = backendContext.drawCommandRange;
    ctx.pipelineKind = pipelineKind;
    ctx.reflectionCapture = passResources.reflectionCapture;
    ctx.reflectionMaterialCapture = passResources.reflectionMaterialCapture;
    ctx.forwardCostMode = RENDER3D::GetRenderQualitySettings().forwardCostMode;
    // record 0 は sentinel。地形 record があるフレームだけ地形 PSO を dispatch する。
    const auto* terrainRecords = gMeshRendererState.gpuDrivenSceneSource.terrainSurfaceGpuData;
    ctx.terrainSurfacesPresent = terrainRecords != nullptr && terrainRecords->size() > 1u;
    ctx.forwardDebugView = frame.view.light.debugView != 0u;
    const bool executed = gMeshRendererState.meshletRenderBackend.Execute(ctx);
    gMeshRendererState.meshletRenderBackend.EndVisibility(graphics);
    UpdateMeshletBackendDebugStats();
    return executed;
}

ID3D12PipelineState* ResolveTraditionalStaticPso(const GFX::Context& graphics, MeshDrawPassKind passKind,
                                                 RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
                                                 const VFX::VariantKey& bucketVariant, bool reflectionCapture,
                                                 bool reflectionMaterialCapture) {
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
    if (passKind == MeshDrawPassKind::WaterVisibility) {
        return gMeshRendererState.pipelines.waterDepthPso.Get();
    }
    if (passKind != MeshDrawPassKind::Forward || graphics.device == nullptr) {
        return GetOrCreateFallbackPso(gMeshRendererState.pipelines, graphics.device, false);
    }

    return GetOrCreateVariantPso(gMeshRendererState.pipelines, graphics.device, gMeshRendererState.debugStats,
                                 bucketVariant, false, false, reflectionCapture, reflectionMaterialCapture);
}

ID3D12PipelineState* ResolveTraditionalSkinnedPso(const GFX::Context& graphics, MeshDrawPassKind passKind,
                                                  RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
                                                  const VFX::VariantKey& bucketVariant, bool reflectionCapture,
                                                  bool reflectionMaterialCapture) {
    (void)gpuPass;

    if (!bucketVariant.vertexShaderId.empty() && bucketVariant.vertexShaderId != "Render3D_StaticVS") {
        return nullptr;
    }
    if (passKind == MeshDrawPassKind::GeometryAux) {
        return gMeshRendererState.pipelines.geometrySkinnedPso.Get();
    }
    if (passKind == MeshDrawPassKind::GeometryAuxFeatureMask) {
        return gMeshRendererState.pipelines.geometryFeatureMaskSkinnedPso.Get();
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
    if (passKind == MeshDrawPassKind::WaterVisibility) {
        return gMeshRendererState.pipelines.waterDepthSkinnedPso.Get();
    }
    if (passKind != MeshDrawPassKind::Forward || graphics.device == nullptr) {
        return GetOrCreateFallbackPso(gMeshRendererState.pipelines, graphics.device, true);
    }

    VFX::VariantKey key = bucketVariant;
    key.vertexShaderId.clear();
    return GetOrCreateVariantPso(gMeshRendererState.pipelines, graphics.device, gMeshRendererState.debugStats, key,
                                 true, false, reflectionCapture, reflectionMaterialCapture);
}

bool PrepareForwardReflectionCapturePipelines(const GFX::Context& graphics, bool captureMaterial) {
    using namespace RENDER3D::GPUDRIVEN;
    if (!graphics.device || !graphics.cmdList)
        return false;
    SyncGpuDrivenBackendAvailability();
    auto& layer = gMeshRendererState.gpuDrivenLayer;
    const auto pass = GpuDrivenPassKind::ForwardOpaque;
    const auto plan = layer.GetPassExecutionPlan(pass);
    if (plan.gpuBackendCount == 0)
        return false;
    for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
        if (plan.gpuBackends[i] == GeometryBackendKind::GpuDrivenMeshShader) {
            if (!gMeshRendererState.meshletRenderBackend.PrepareReflectionCapture(captureMaterial))
                return false;
            continue;
        }
        if (plan.gpuBackends[i] != GeometryBackendKind::GpuDrivenTraditionalVsPs)
            return false;
        const auto backend = layer.BuildGeometryBackendContext(graphics.cmdList, pass, plan.gpuBackends[i]);
        const auto* range = backend.drawCommandRange;
        if (!range || !range->traditionalIndirect || !range->traditionalIndirect->bucketVariants)
            return false;
        const auto& variants = *range->traditionalIndirect->bucketVariants;
        if (range->commandBucketCount > variants.size())
            return false;
        for (size_t bucket = 0; bucket < range->commandBucketCount; ++bucket) {
            if (range->staticCommandCount && !ResolveTraditionalStaticPso(graphics, MeshDrawPassKind::Forward, pass,
                                                                          variants[bucket], true, captureMaterial))
                return false;
            if (range->skinnedCommandCount && !ResolveTraditionalSkinnedPso(graphics, MeshDrawPassKind::Forward, pass,
                                                                            variants[bucket], true, captureMaterial))
                return false;
        }
    }
    return true;
}

bool ExecuteTraditionalDrawFrame(const GFX::Context& graphics, const MeshPassResources& passResources,
                                 RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass, MeshDrawPassKind passKind,
                                 RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride,
                                 const MeshFrameBindingOverrides* bindingOverrides) {
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
        layerOverride != nullptr ? *layerOverride : gMeshRendererState.gpuDrivenLayer;
    if (layerOverride == nullptr) {
        SyncGpuDrivenBackendAvailability();
    }
    if (!layer.IsPassGpuReady(gpuPass)) {
        return false;
    }

    const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext = layer.BuildGeometryBackendContext(
        graphics.cmdList, gpuPass, RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs);
    const RENDER3D::GPUDRIVEN::GpuDrivenDrawCommandRange* range = backendContext.drawCommandRange;
    if (!range)
        return false;
    const auto staticStream = range->TraditionalStream(false);
    const auto skinnedStream = range->TraditionalStream(true);
    if (!staticStream && !skinnedStream)
        return false;
    const auto* bucketVariants = range->traditionalIndirect->bucketVariants;

    MeshBindingStateCache bindingCache{};
    const bool depthAwarePhase = gpuPass == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware;
    MeshDrawContext drawCtx = BuildDrawContext(graphics, depthAwarePhase, passKind, passResources, bindingOverrides);
    drawCtx.binding.cache = &bindingCache;
    drawCtx.surfaceGpuSceneBaseOffset = range->gpuSceneBaseIndex;
    BindSurfaceRecordFrameResources(drawCtx);
    BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
    BindObjectDataIndex(drawCtx.binding, 0u);
    BindMaterialDataIndex(drawCtx.binding, 0u);
    bool executed = false;
    if (staticStream) {
        for (size_t bucketIndex = 0; bucketIndex < staticStream->bucketCount; ++bucketIndex) {
            const VFX::VariantKey& bucketVariant = (*bucketVariants)[bucketIndex];
            ID3D12PipelineState* bucketPso =
                ResolveTraditionalStaticPso(graphics, passKind, gpuPass, bucketVariant, passResources.reflectionCapture,
                                            passResources.reflectionMaterialCapture);
            if (bucketPso == nullptr) {
                continue;
            }
            BindPipelineState(drawCtx.binding, bucketPso);
            staticStream->Execute(graphics.cmdList, bucketIndex);
            executed = true;
        }
    }

    if (skinnedStream && drawCtx.skinnedRootSig != nullptr) {
        BindFrameCommonResources(drawCtx.binding, drawCtx.skinnedRootSig, drawCtx.cameraAddress,
                                 drawCtx.cullingCameraAddress, drawCtx.lightAddress, drawCtx.localLightsAddress,
                                 drawCtx.localLightClusterHeadersAddress, drawCtx.localLightClusterIndicesAddress,
                                 drawCtx.materialProgramParametersAddress, drawCtx.materialProgramResourcesAddress,
                                 drawCtx.shadowAddress, drawCtx.skyEnvironmentAddress);
        BindObjectDataBuffer(drawCtx.binding, drawCtx.objectDataSrv);
        BindMaterialDataBuffer(drawCtx.binding, drawCtx.materialDataSrv);
        BindSurfaceGpuSceneBuffer(drawCtx.binding, drawCtx.surfaceGpuSceneSrv);
        BindSurfaceGpuSceneControl(drawCtx.binding, 0u, false);
        BindSurfaceFeatureData(drawCtx.binding, drawCtx.surfaceFeatureDataAddress);
        BindTerrainSurfaceData(drawCtx.binding, drawCtx.terrainSurfaceDataAddress);
        BindSurfaceFeatureFloats(drawCtx.binding, drawCtx.surfaceFeatureFloatsAddress);
        BindShadowMap(drawCtx.binding);
        if (passKind == MeshDrawPassKind::Forward) {
            BindSkyCube(drawCtx.binding);
            BindSceneDepth(drawCtx.binding);
            BindSceneColor(drawCtx.binding);
            BindIblResources(drawCtx.binding);
            BindReflectionProbeResources(drawCtx.binding);
            BindSsao(drawCtx.binding);
            BindLightProbeResources(drawCtx.binding);
        } else if (passKind == MeshDrawPassKind::WaterVisibility) {
            BindSceneDepth(drawCtx.binding);
        }
        BindObjectDataIndex(drawCtx.binding, 0u);
        BindMaterialDataIndex(drawCtx.binding, 0u);
        for (size_t bucketIndex = 0; bucketIndex < skinnedStream->bucketCount; ++bucketIndex) {
            const VFX::VariantKey& bucketVariant = (*bucketVariants)[bucketIndex];
            ID3D12PipelineState* bucketPso =
                ResolveTraditionalSkinnedPso(graphics, passKind, gpuPass, bucketVariant,
                                             passResources.reflectionCapture, passResources.reflectionMaterialCapture);
            if (bucketPso == nullptr) {
                continue;
            }
            BindPipelineState(drawCtx.binding, bucketPso);
            skinnedStream->Execute(graphics.cmdList, bucketIndex);
            executed = true;
        }
    }

    gMeshRendererState.debugStats.gpuDrivenSkinnedCommandCount += range->skinnedCommandCount;
    gMeshRendererState.debugStats.gpuDrivenSkinnedSourceRecordCount += range->skinnedCommandCount;
    return executed;
}

bool ExecuteGeometryBackend(const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
                            RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass, const MeshPassResources& passResources,
                            MeshDrawPassKind passKind, RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
                            RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
                            const MeshFrameBindingOverrides* bindingOverrides = nullptr) {
    switch (backend) {
    case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader:
        return ExecuteMeshletDrawFrame(graphics, passResources, gpuPass, passKind, meshletPipelineKind, layerOverride,
                                       bindingOverrides);
    case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs:
        return ExecuteTraditionalDrawFrame(graphics, passResources, gpuPass, passKind, layerOverride, bindingOverrides);
    default: return false;
    }
}

GeometryBackendExecutionResult ExecuteGeometryBackendPlan(const GFX::Context& graphics,
                                                          RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
                                                          const MeshPassResources& passResources,
                                                          MeshDrawPassKind passKind,
                                                          RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind) {
    GeometryBackendExecutionResult result{};
    SyncGpuDrivenBackendAvailability();
    const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
        gMeshRendererState.gpuDrivenLayer.GetPassExecutionPlan(pass);
    for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
        const RENDER3D::GPUDRIVEN::GeometryBackendKind backend = plan.gpuBackends[i];
        if (!ExecuteGeometryBackend(graphics, backend, pass, passResources, passKind, meshletPipelineKind)) {
            continue;
        }
        result.RecordExecutedBackend(backend);
    }
    return result;
}

GeometryBackendExecutionResult ExecuteSecondaryViewGeometryBackendPlan(
    const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass, const MeshPassResources& passResources,
    MeshDrawPassKind passKind, RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer, const MeshFrameBindingOverrides& bindingOverrides,
    bool allowCompatibilityFallback) {
    GeometryBackendExecutionResult result{};
    RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan = layer.GetPassExecutionPlan(pass);
    // Runtime secondary views must remain usable while an imported or
    // asynchronously clustered surface is still traditional-only. The
    // primary view keeps the selected product policy; this bounded view
    // may consume the already-owned compatibility stream instead of
    // rejecting the complete capture frame.
    if (allowCompatibilityFallback && plan.gpuBackendCount == 0u &&
        layer.IsBackendConsumable(pass, RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs)) {
        (void)plan.AddGpuBackend(RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs);
    }
    for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
        const RENDER3D::GPUDRIVEN::GeometryBackendKind backend = plan.gpuBackends[i];
        if (!ExecuteGeometryBackend(graphics, backend, pass, passResources, passKind, meshletPipelineKind, &layer,
                                    &bindingOverrides)) {
            continue;
        }
        result.RecordExecutedBackend(backend);
    }
    return result;
}

#if defined(HIKARI_WITH_EDITOR)
GeometryBackendExecutionResult ExecuteEditorInteractiveGeometryBackendPlan(
    const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass, const MeshPassResources& passResources,
    MeshDrawPassKind passKind, RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer, const MeshFrameBindingOverrides& bindingOverrides) {
    return ExecuteSecondaryViewGeometryBackendPlan(graphics, pass, passResources, passKind, meshletPipelineKind, layer,
                                                   bindingOverrides);
}
#endif

GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(const GFX::Context& graphics,
                                                                 RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
                                                                 const MeshPassResources& passResources,
                                                                 const MeshFrameBindingOverrides* bindingOverrides) {
    GeometryBackendExecutionResult result{};
    SyncGpuDrivenBackendAvailability();
    const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
        gMeshRendererState.gpuDrivenLayer.GetPassExecutionPlan(pass);
    for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
        const RENDER3D::GPUDRIVEN::GeometryBackendKind backend = plan.gpuBackends[i];
        if (!ExecuteGeometryBackend(graphics, backend, pass, passResources, MeshDrawPassKind::DepthPrepass,
                                    RENDER3D::MESHLET::MeshletPipelineKind::DepthPrepass, nullptr, bindingOverrides)) {
            continue;
        }
        result.RecordExecutedBackend(backend);
    }
    return result;
}

} // namespace HIKARI::MESHRENDERER::INTERNAL
