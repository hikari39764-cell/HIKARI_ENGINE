#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "HIKARI_Services.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    void UploadGpuDrivenSceneFrame() {
        gMeshRendererState.gpuDrivenLayer.BeginFrame(&gMeshRendererState.gpuDrivenSceneSource);

        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
        uploadDesc.commandList = SERVICES::gCtx.cmdList;
        uploadDesc.residency = &gMeshRendererState.gpuDrivenSceneResidency;
        uploadDesc.frameIndex = SERVICES::gCtx.frameIndex;
        const std::span<const uint32_t> materialBindings =
            gMeshRendererState.gpuMaterialRegistry.GetSourceBindings();
        uploadDesc.materialSlotBySourceRecord = materialBindings.data();
        uploadDesc.materialSourceRecordCount = materialBindings.size();
        uploadDesc.materialBindingVersion =
            gMeshRendererState.gpuMaterialRegistry.GetBindingVersion();
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            gMeshRendererState.gpuDrivenLayer.UploadSceneFrame(uploadDesc);

        gMeshRendererState.debugStats.surfaceGpuSceneOpaqueInstanceCount =
            uploadStats.passInstanceCounts[
                RENDER3D::GPUDRIVEN::ToPassIndex(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)];
        gMeshRendererState.debugStats.surfaceGpuSceneDepthPrepassInstanceCount =
            uploadStats.passInstanceCounts[
                RENDER3D::GPUDRIVEN::ToPassIndex(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass)];
        gMeshRendererState.debugStats.surfaceGpuSceneDepthAwareInstanceCount =
            uploadStats.passInstanceCounts[
                RENDER3D::GPUDRIVEN::ToPassIndex(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)];
        gMeshRendererState.debugStats.surfaceGpuSceneTransparentInstanceCount =
            uploadStats.passInstanceCounts[
                RENDER3D::GPUDRIVEN::ToPassIndex(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)];
        gMeshRendererState.debugStats.surfaceGpuSceneShadowInstanceCount =
            uploadStats.passInstanceCounts[
                RENDER3D::GPUDRIVEN::ToPassIndex(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow)];

        const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
            uploadStats.bufferStats;
        gMeshRendererState.debugStats.surfaceGpuSceneCapacity = gpuSceneStats.capacity;
        gMeshRendererState.debugStats.surfaceGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
        gMeshRendererState.debugStats.surfaceGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
        gMeshRendererState.debugStats.surfaceGpuSceneCommittedInstanceCount = gpuSceneStats.committedInstanceCount;
        gMeshRendererState.debugStats.surfaceGpuSceneCommittedBytes = gpuSceneStats.committedBytes;
        gMeshRendererState.debugStats.surfaceGpuSceneFullUploadCount =
            uploadStats.uploadedFullScene ? 1u : 0u;
        gMeshRendererState.debugStats.surfaceGpuSceneDirtyPatchCount =
            uploadStats.patchedDirtyRanges ? 1u : 0u;
        gMeshRendererState.debugStats.surfaceGpuSceneReuseCount =
            uploadStats.reusedResidentFrame ? 1u : 0u;
        gMeshRendererState.debugStats.surfaceGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
        gMeshRendererState.debugStats.surfaceGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
        gMeshRendererState.debugStats.surfaceGpuSceneMaterialPatchCount =
            gpuSceneStats.materialBindingVisitCount;
        gMeshRendererState.debugStats.surfaceGpuSceneMaterialPatchChangedCount =
            gpuSceneStats.materialPatchChangedCount;
        gMeshRendererState.debugStats.surfaceGpuSceneMaterialPatchUnchangedCount =
            gpuSceneStats.materialPatchUnchangedCount;
        gMeshRendererState.debugStats.surfaceGpuSceneMaterialPatchFailCount =
            gpuSceneStats.materialBindingMissingCount;
        gMeshRendererState.debugStats.surfaceGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
        gMeshRendererState.debugStats.surfaceGpuSceneBufferReady = gpuSceneStats.initialized;
    }

    void CommitActiveMaterialDataFrame(ID3D12GraphicsCommandList* commandList) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::MaterialPrepare);
        MeshFrameResources& frame = GetActiveMeshFrameResources();
        if (!gMeshRendererState.gpuMaterialRegistry.StageFrame(
                gMeshRendererState.frameResources.GetActiveFrameIndex(),
                frame.materialDataMapped,
                kMaxMaterialDataCount,
                gMeshRendererState.materialUploadRanges)) {
            return;
        }

        gMeshRendererState.frameResources.CommitActiveMaterialRanges(
            commandList,
            sizeof(MaterialGpuData),
            gMeshRendererState.materialUploadRanges);

        const RENDER3D::MATERIAL::GpuMaterialRegistryStats& stats =
            gMeshRendererState.gpuMaterialRegistry.GetStats();
        gMeshRendererState.debugStats.materialDataGpuUploadBytes = stats.frameUploadBytes;
        gMeshRendererState.debugStats.materialDataGpuUploadCallCount =
            stats.frameUploadRangeCount;
        gMeshRendererState.debugStats.materialDataWriteCount = stats.frameUpdatedSlotCount;
        gMeshRendererState.debugStats.materialDataCacheHitCount = stats.frameSourceReuseCount;
        gMeshRendererState.debugStats.materialDataCacheMissCount =
            stats.frameCreatedSlotCount + stats.frameUpdatedSlotCount;
        gMeshRendererState.debugStats.materialDataOverflowCount = stats.frameOverflowCount;
        gMeshRendererState.debugStats.materialDataCachedCount = stats.residentSlotCount;
    }

    void SyncSurfaceGpuSceneMaterialFrame() {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::MaterialPrepare);

        const RENDER3D::MATERIAL::GpuMaterialSourceSyncMode syncMode =
            gMeshRendererState.gpuMaterialRegistry.BeginSourceSync(
                reinterpret_cast<uintptr_t>(gMeshRendererState.gpuDrivenSceneSourceIdentity),
                gMeshRendererState.gpuDrivenSceneSource.layoutVersion,
                gMeshRendererState.gpuDrivenSceneSource.sourceVersion,
                gMeshRendererState.gpuDrivenSceneSource.dirtyBaseSourceVersion,
                gMeshRendererState.gpuDrivenSceneSource.sourceRecordCount);
        if (syncMode ==
            RENDER3D::MATERIAL::GpuMaterialSourceSyncMode::None) {
            return;
        }

        MeshDrawContext drawCtx = BuildDrawContext(false, MeshDrawPassKind::Forward, {});
        const auto prepareMaterialSources =
            [&](uint32_t baseIndex,
                const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sources,
                size_t firstSource,
                size_t sourceCount) {
            drawCtx.surfaceGpuSceneBaseOffset = baseIndex;
            if (sources != nullptr &&
                firstSource < sources->size() &&
                sourceCount != 0u) {
                const size_t clampedCount =
                    (std::min)(sourceCount, sources->size() - firstSource);
                PrepareSurfaceGpuSceneMaterialSources(
                    drawCtx,
                    sources->data() + firstSource,
                    clampedCount);
            }
        };

        constexpr std::array<RENDER3D::GPUDRIVEN::GpuDrivenPassKind, 4>
            kMaterialPasses{
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
            };

        if (syncMode ==
            RENDER3D::MATERIAL::GpuMaterialSourceSyncMode::Incremental) {
            for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind :
                kMaterialPasses) {
                const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                    GetSceneSourcePass(passKind);
                for (const RENDER3D::GPUDRIVEN::GpuSceneDirtyRange& range :
                    pass.dirtyRanges) {
                    prepareMaterialSources(
                        pass.gpuSceneBaseIndex,
                        pass.materialSources,
                        range.firstInstance,
                        range.instanceCount);
                }
            }
        } else {
            std::vector<const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>*>
                visitedPrimarySources{};
            for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind :
                kMaterialPasses) {
                const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                    GetSceneSourcePass(passKind);
                if (pass.materialSources != nullptr &&
                    std::find(
                        visitedPrimarySources.begin(),
                        visitedPrimarySources.end(),
                        pass.materialSources) == visitedPrimarySources.end()) {
                    visitedPrimarySources.push_back(pass.materialSources);
                    prepareMaterialSources(
                        pass.gpuSceneBaseIndex,
                        pass.materialSources,
                        0u,
                        pass.materialSources->size());
                }
                prepareMaterialSources(
                    pass.traditionalIndirect.gpuSceneBaseIndex,
                    pass.traditionalIndirect.materialSources,
                    0u,
                    pass.traditionalIndirect.materialSources != nullptr
                        ? pass.traditionalIndirect.materialSources->size()
                        : 0u);
            }
        }

        gMeshRendererState.gpuMaterialRegistry.EndSourceSync();
    }

    void UpdateTraditionalCommandStreamStats() {
        const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
            gMeshRendererState.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
        gMeshRendererState.debugStats.traditionalCommandStreamCommandCapacity = indirectStats.capacity;
        gMeshRendererState.debugStats.traditionalCommandStreamRequestedCommandCount = indirectStats.requestedCommandCount;
        gMeshRendererState.debugStats.traditionalCommandStreamUploadedCommandCount = indirectStats.uploadedCommandCount;
        gMeshRendererState.debugStats.traditionalCommandStreamOverflowCommandCount = indirectStats.overflowCommandCount;
        gMeshRendererState.debugStats.traditionalCommandStreamMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
        gMeshRendererState.debugStats.traditionalCommandStreamUploadCallCount = indirectStats.uploadCallCount;
        gMeshRendererState.debugStats.traditionalCommandStreamInputUploadBytes =
            indirectStats.inputUploadBytes;
        gMeshRendererState.debugStats.traditionalCommandStreamInputUploadCopyCount =
            indirectStats.inputUploadCopyCount;
        gMeshRendererState.debugStats.traditionalCommandStreamResidentInputReuseCount =
            indirectStats.reusedResidentInput ? 1u : 0u;
        gMeshRendererState.debugStats.traditionalCommandStreamCommandStride = indirectStats.commandStride;
        gMeshRendererState.debugStats.traditionalCommandStreamArgumentBufferReady = indirectStats.initialized;
        gMeshRendererState.debugStats.traditionalCommandStreamCommandSignatureReady = indirectStats.commandSignatureReady;
    }

    void UpdateGpuDrivenWorklistDebugStats() {
        gMeshRendererState.debugStats.gpuDrivenWorklistPassCount =
            gMeshRendererState.gpuDrivenFrame.CountActivePasses();
        gMeshRendererState.debugStats.gpuDrivenWorklistClusterPassCount =
            gMeshRendererState.gpuDrivenFrame.CountClusterEligiblePasses();
        gMeshRendererState.debugStats.gpuDrivenWorklistSourceInstanceCount =
            gMeshRendererState.gpuDrivenFrame.CountSourceInstances();
        gMeshRendererState.debugStats.gpuDrivenWorklistClusterInstanceCount =
            gMeshRendererState.gpuDrivenFrame.CountClusterEligibleInstances();
    }


} // namespace HIKARI::MESHRENDERER::INTERNAL
