#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#include <algorithm>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    uint32_t MakePreDepthGpuDrivenPassMask() {
        using RENDER3D::GPUDRIVEN::GpuDrivenPassKind;
        using RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask;
        // DepthPrepass は HZB 構築前に描くため、occlusion なし (frustum のみ)
        // の BeginFrame ビルドで work を確定させる。
        // Shadow はここに含めない: shadow map は ShadowMapRenderer が光源
        // 行列で専用の cull チェーンを回すため、主カメラでの Shadow cull は
        // 消費者が存在しない。
        return MakeGpuDrivenPassMask(GpuDrivenPassKind::DepthPrepass);
    }

    uint32_t MakeMainCameraGpuDrivenPassMask() {
        using RENDER3D::GPUDRIVEN::GpuDrivenPassKind;
        using RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask;
        // DepthPrepass も finalize で再ビルドする。visibility dispatch は
        // frameContext を丸ごと差し替えるため、ここに含めないと BeginFrame
        // で作った prepass work が finalize 後に消える。
        return
            MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardOpaque) |
            MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardDepthAware) |
            MakeGpuDrivenPassMask(GpuDrivenPassKind::ForwardTransparent) |
            MakeGpuDrivenPassMask(GpuDrivenPassKind::GeometryAux) |
            MakeGpuDrivenPassMask(GpuDrivenPassKind::DepthPrepass);
    }

    void BuildStrictGpuDrivenCommandFrame() {
        const MATH::Mat4 viewProj = ResolveGpuDrivenCullingViewProj();
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
        commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
        commandFrameDesc.cullViewProj = &viewProj;
        commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
        gMeshRendererState.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
        UpdateTraditionalCommandStreamStats();
        UpdateGpuDrivenCommandStreamDebugStats();
    }

    void BuildGpuDrivenFrameState() {
        const RENDER3D::GPUDRIVEN::GpuDrivenFrameBuildInput input =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(
                gMeshRendererState.gpuDrivenSceneSource);
        gMeshRendererState.gpuDrivenFrame =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);
        UpdateGpuDrivenWorklistDebugStats();
    }

    void ResetGpuDrivenFrameState() {
        UploadGpuDrivenSceneFrame();
        gMeshRendererState.gpuDrivenFrame.Reset();
        UpdateGpuDrivenWorklistDebugStats();
        gMeshRendererState.clusterGpuDrivenProducer.BeginFrame(false);
        gMeshRendererState.gpuDrivenLayer.ImportProducerOutput(
            gMeshRendererState.clusterGpuDrivenProducer.BuildFrameOutput());
        gMeshRendererState.meshletRenderBackend.ResetFrame();
        UpdateMeshletBackendDebugStats();
        UpdateGpuDrivenWorkReadyDebugStats();
        UpdateGpuDrivenWorkOwnershipDebugStats();
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
        commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
        commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
        gMeshRendererState.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
        UpdateTraditionalCommandStreamStats();
        UpdateGpuDrivenCommandStreamDebugStats();
    }

    void PrepareGpuDrivenFrameState() {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::MeshPrepare);
        UploadGpuDrivenSceneFrame();
        if (!gMeshRendererState.gpuDrivenSceneResidency.resident) {
            gMeshRendererState.gpuDrivenFrame.Reset();
            UpdateGpuDrivenWorklistDebugStats();
            gMeshRendererState.clusterGpuDrivenProducer.BeginFrame(false);
            gMeshRendererState.gpuDrivenLayer.ImportProducerOutput(
                gMeshRendererState.clusterGpuDrivenProducer.BuildFrameOutput());
            gMeshRendererState.meshletRenderBackend.ResetFrame();
            UpdateMeshletBackendDebugStats();
            UpdateGpuDrivenWorkReadyDebugStats();
            UpdateGpuDrivenWorkOwnershipDebugStats();
            RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
            commandFrameDesc.commandList = SERVICES::gCtx.cmdList;
            commandFrameDesc.frameIndex = SERVICES::gCtx.frameIndex;
            gMeshRendererState.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);
            UpdateTraditionalCommandStreamStats();
            UpdateGpuDrivenCommandStreamDebugStats();
            return;
        }
        BuildGpuDrivenFrameState();
        BuildGpuDrivenWorkFrame(
            nullptr,
            MakePreDepthGpuDrivenPassMask(),
            false);
        gMeshRendererState.meshletRenderBackend.ResetFrame();
        UpdateMeshletBackendDebugStats();
        UpdateGpuDrivenWorkReadyDebugStats();
        UpdateGpuDrivenWorkOwnershipDebugStats();
        BuildStrictGpuDrivenCommandFrame();
    }

    void BuildGpuDrivenWorkFrame(
        const HIKARI::RENDER3D::DEPTH::DepthPyramidView* depthPyramid,
        uint32_t passMask,
        bool collectCounterReadback) {
        const MATH::Mat4 viewProj = ResolveGpuDrivenCullingViewProj();
        const MATH::Vec3 cameraPosition = ResolveGpuDrivenCullingCameraPosition();
        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (SERVICES::gCtx.cmdList != nullptr && srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            SERVICES::gCtx.cmdList->SetDescriptorHeaps(1, heaps);
        }
        RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
        workContext.producer = &gMeshRendererState.clusterGpuDrivenProducer;
        workContext.commandList = SERVICES::gCtx.cmdList;
        workContext.viewProj = viewProj;
        workContext.cameraPosition = cameraPosition;
        workContext.geometryPoolSrv = RENDER3D::GetClusterGeometryPoolSrvGpuHandle(SERVICES::gCtx);
        workContext.surfaceGpuSceneGpuAddress =
            gMeshRendererState.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
        workContext.frame = &gMeshRendererState.gpuDrivenFrame;
        workContext.passMask = passMask;
        workContext.collectCounterReadback = collectCounterReadback;
        if (depthPyramid != nullptr &&
            depthPyramid->valid &&
            depthPyramid->pyramidSrv.ptr != 0 &&
            depthPyramid->width != 0 &&
            depthPyramid->height != 0 &&
            depthPyramid->viewProjValid) {

            workContext.depthOcclusion.enabled = true;
            workContext.depthOcclusion.hzbSrv = depthPyramid->pyramidSrv;
            workContext.depthOcclusion.hzbWidth = depthPyramid->width;
            workContext.depthOcclusion.hzbHeight = depthPyramid->height;
            workContext.depthOcclusion.hzbMipCount =
                std::max(1u, depthPyramid->mipCount);
            workContext.depthOcclusion.hzbViewProj = depthPyramid->viewProj;
            workContext.depthOcclusion.hzbViewProjValid = true;
            workContext.depthOcclusion.depthPyramid = *depthPyramid;
        }
        (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);

        const RENDER3D::CLUSTER::ClusterGpuCullingPassStats* clusterCullStatsPtr =
            gMeshRendererState.clusterGpuDrivenProducer.GetClusterStats();
        const RENDER3D::CLUSTER::ClusterGpuCullingPassStats fallbackClusterCullStats{};
        const RENDER3D::CLUSTER::ClusterGpuCullingPassStats& clusterCullStats =
            clusterCullStatsPtr != nullptr
                ? *clusterCullStatsPtr
                : fallbackClusterCullStats;
        gMeshRendererState.gpuDrivenLayer.ImportProducerOutput(
            gMeshRendererState.clusterGpuDrivenProducer.BuildFrameOutput());
        gMeshRendererState.gpuDrivenLayer.BuildCommandBuffers();
        UpdateGpuDrivenCommandStreamDebugStats();
        gMeshRendererState.debugStats.clusterGpuCullReady =
            clusterCullStats.initialized &&
            clusterCullStats.psoReady &&
            clusterCullStats.inputBufferReady &&
            clusterCullStats.visibleRangeBufferReady &&
            clusterCullStats.counterBufferReady;
        gMeshRendererState.debugStats.clusterGpuCullSourceInstanceCount =
            clusterCullStats.sourceInstanceCount;
        gMeshRendererState.debugStats.clusterGpuCullCandidateInstanceCount =
            clusterCullStats.candidateInstanceCount;
        gMeshRendererState.debugStats.clusterGpuCullSubmittedInstanceCount =
            clusterCullStats.submittedInstanceCount;
        gMeshRendererState.debugStats.clusterGpuCullSourcePageTaskCount =
            clusterCullStats.sourcePageTaskCount;
        gMeshRendererState.debugStats.clusterGpuCullSubmittedPageTaskCount =
            clusterCullStats.submittedPageTaskCount;
        gMeshRendererState.debugStats.clusterGpuCullDrawSeedCount =
            clusterCullStats.submittedDrawSeedCount;
        gMeshRendererState.debugStats.clusterGpuCullOverflowInstanceCount =
            clusterCullStats.overflowInstanceCount;
        gMeshRendererState.debugStats.clusterGpuCullCounterReadbackReady =
            clusterCullStats.gpuCounterReadbackReady;
        gMeshRendererState.debugStats.clusterGpuCullCounterReadbackValid =
            clusterCullStats.gpuCounterReadbackValid;
        gMeshRendererState.debugStats.clusterGpuCullDebugCountersEnabled =
            clusterCullStats.debugCountersEnabled;
        gMeshRendererState.debugStats.clusterGpuCullFineCullingOwnedByAmplificationShader =
            clusterCullStats.meshletFineCullingDeferredToAmplificationShader;
        gMeshRendererState.debugStats.clusterGpuCullOcclusionHistoryReady =
            clusterCullStats.occlusionHistoryReady;
        gMeshRendererState.debugStats.clusterGpuCullGpuInputCount =
            clusterCullStats.gpuInputCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageTaskCount =
            clusterCullStats.gpuPageTaskCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageTaskOverflowCount =
            clusterCullStats.gpuPageTaskOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuVisibleRangeCount =
            clusterCullStats.gpuVisibleRangeCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuVisibleClusterCount =
            clusterCullStats.gpuVisibleClusterCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuOverflowCount =
            clusterCullStats.gpuOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullHzbOcclusionEnabled =
            clusterCullStats.hzbOcclusionEnabled;
        gMeshRendererState.debugStats.clusterGpuCullHzbOcclusionWidth =
            clusterCullStats.hzbOcclusionWidth;
        gMeshRendererState.debugStats.clusterGpuCullHzbOcclusionHeight =
            clusterCullStats.hzbOcclusionHeight;
        gMeshRendererState.debugStats.clusterGpuCullHzbOcclusionMipCount =
            clusterCullStats.hzbOcclusionMipCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuInputFrustumCulledCount =
            clusterCullStats.gpuInputFrustumCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageTestedCount =
            clusterCullStats.gpuPageTestedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageFrustumCulledCount =
            clusterCullStats.gpuPageFrustumCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageOcclusionTestedCount =
            clusterCullStats.gpuPageOcclusionTestedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageOcclusionCulledCount =
            clusterCullStats.gpuPageOcclusionCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterTestedCount =
            clusterCullStats.gpuClusterTestedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterFrustumCulledCount =
            clusterCullStats.gpuClusterFrustumCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterOcclusionTestedCount =
            clusterCullStats.gpuClusterOcclusionTestedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterOcclusionCulledCount =
            clusterCullStats.gpuClusterOcclusionCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbPassRejectedCount =
            clusterCullStats.gpuHzbPassRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbAabbRejectedCount =
            clusterCullStats.gpuHzbAabbRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbSphereRejectedCount =
            clusterCullStats.gpuHzbSphereRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbQueryAcceptedCount =
            clusterCullStats.gpuHzbQueryAcceptedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbTryCount =
            clusterCullStats.gpuHzbTryCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbAllowedCount =
            clusterCullStats.gpuHzbAllowedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbInvalidRejectedCount =
            clusterCullStats.gpuHzbInvalidRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbNearPlaneRejectedCount =
            clusterCullStats.gpuHzbNearPlaneRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbOffscreenRejectedCount =
            clusterCullStats.gpuHzbOffscreenRejectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbLargeRectCount =
            clusterCullStats.gpuHzbLargeRectCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbAabbAcceptedCount =
            clusterCullStats.gpuHzbAabbAcceptedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbSphereAcceptedCount =
            clusterCullStats.gpuHzbSphereAcceptedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbRawOccludedCount =
            clusterCullStats.gpuHzbRawOccludedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbTemporalPendingCount =
            clusterCullStats.gpuHzbTemporalPendingCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbTemporalConfirmedCount =
            clusterCullStats.gpuHzbTemporalConfirmedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbTemporalResetCount =
            clusterCullStats.gpuHzbTemporalResetCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbTemporalCollisionCount =
            clusterCullStats.gpuHzbTemporalCollisionCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbLargeRectSkippedCount =
            clusterCullStats.gpuHzbLargeRectSkippedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPageHzbSmallScreenSkippedCount =
            clusterCullStats.gpuPageHzbSmallScreenSkippedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterHzbSmallScreenSkippedCount =
            clusterCullStats.gpuClusterHzbSmallScreenSkippedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuConeSkippedDoubleSidedCount =
            clusterCullStats.gpuConeSkippedDoubleSidedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuConeSkippedMaterialCount =
            clusterCullStats.gpuConeSkippedMaterialCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterHzbLargeScreenSkippedCount =
            clusterCullStats.gpuClusterHzbLargeScreenSkippedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuHzbBudgetSkippedCount =
            clusterCullStats.gpuHzbBudgetSkippedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterConeCulledCount =
            clusterCullStats.gpuClusterConeCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuClusterConeTestedCount =
            clusterCullStats.gpuClusterConeTestedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuDoubleSidedClusterCount =
            clusterCullStats.gpuDoubleSidedClusterCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuDrawCommandCount =
            clusterCullStats.gpuDrawCommandCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuBackFaceDrawCommandCount =
            clusterCullStats.gpuBackFaceDrawCommandCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuDoubleSidedDrawCommandCount =
            clusterCullStats.gpuDoubleSidedDrawCommandCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuDrawCommandOverflowCount =
            clusterCullStats.gpuDrawCommandOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuBackFaceDrawCommandOverflowCount =
            clusterCullStats.gpuBackFaceDrawCommandOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuDoubleSidedDrawCommandOverflowCount =
            clusterCullStats.gpuDoubleSidedDrawCommandOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuMergedGapCount =
            clusterCullStats.gpuMergedGapCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuMergedGapIndexCount =
            clusterCullStats.gpuMergedGapIndexCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPacketRangeCount =
            clusterCullStats.gpuPacketRangeCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuPacketClusterCount =
            clusterCullStats.gpuPacketClusterCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuVisibleClusterListReservedCount =
            clusterCullStats.gpuVisibleClusterListReservedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuVisibleClusterListOverflowCount =
            clusterCullStats.gpuVisibleClusterListOverflowCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuLod0SelectedCount =
            clusterCullStats.gpuLod0SelectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuLod1SelectedCount =
            clusterCullStats.gpuLod1SelectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuLod2SelectedCount =
            clusterCullStats.gpuLod2SelectedCount;
        gMeshRendererState.debugStats.clusterGpuCullGpuLod3PlusSelectedCount =
            clusterCullStats.gpuLod3PlusSelectedCount;
        gMeshRendererState.debugStats.clusterGpuCullLodTargetErrorNdc =
            clusterCullStats.lodTargetErrorNdc;
        gMeshRendererState.debugStats.clusterGpuCullLodTransitionRelaxPerLevel =
            clusterCullStats.lodTransitionRelaxPerLevel;
        gMeshRendererState.debugStats.clusterGpuCullLodErrorRelaxPerLevel =
            clusterCullStats.lodErrorRelaxPerLevel;
        gMeshRendererState.debugStats.clusterGpuCullGpuCulledInstanceCount =
            clusterCullStats.gpuInputFrustumCulledCount;
        gMeshRendererState.debugStats.clusterGpuCullDispatchCount =
            clusterCullStats.dispatchCount;
        gMeshRendererState.debugStats.clusterGpuCullWorkgroupCount =
            clusterCullStats.workgroupCount;
        gMeshRendererState.debugStats.clusterGpuCullInputCapacity =
            clusterCullStats.inputCapacity;
        gMeshRendererState.debugStats.clusterGpuCullVisibleRangeCapacity =
            clusterCullStats.visibleRangeCapacity;
        gMeshRendererState.debugStats.clusterGpuCullCandidateCommandCapacity =
            clusterCullStats.candidateCommandCapacity;
        gMeshRendererState.debugStats.clusterGpuCullOcclusionHistoryCapacity =
            clusterCullStats.occlusionHistoryCapacity;
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
