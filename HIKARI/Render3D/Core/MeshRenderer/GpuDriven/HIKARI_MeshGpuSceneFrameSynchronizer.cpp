#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    void UploadGpuDrivenSceneFrame(const GFX::Context& graphics) {
        gMeshRendererState.gpuDrivenLayer.BeginFrame(&gMeshRendererState.gpuDrivenSceneSource);

        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
        uploadDesc.commandList = graphics.cmdList;
        uploadDesc.residency = &gMeshRendererState.gpuDrivenSceneResidency;
        uploadDesc.frameIndex = graphics.frameIndex;
        const std::span<const uint32_t> materialBindings =
            gMeshRendererState.gpuMaterialRegistry.GetSourceBindings();
        uploadDesc.materialSlotBySourceRecord = materialBindings.data();
        uploadDesc.materialSourceRecordCount = materialBindings.size();
        uploadDesc.materialBindingVersion =
            gMeshRendererState.gpuMaterialRegistry.GetBindingVersion();
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            gMeshRendererState.gpuDrivenLayer.UploadSceneFrame(uploadDesc);
        gMeshRendererState.debugStats.gpuSceneUpload = uploadStats;
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
    }

    void SyncSurfaceGpuSceneMaterialFrame(const GFX::Context& graphics) {
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

        MeshDrawContext drawCtx =
            BuildDrawContext(graphics, false, MeshDrawPassKind::Forward, {});
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
        gMeshRendererState.debugStats.traditionalCommandStream = indirectStats;
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
