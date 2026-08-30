#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    void UploadGpuDrivenSceneFrame(const GFX::Context& graphics) {
        gMeshRendererState.gpuDrivenLayer.BeginFrame(&gMeshRendererState.gpuDrivenSceneSource);

        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
        uploadDesc.commandList = graphics.cmdList;
        uploadDesc.residency = &gMeshRendererState.gpuDrivenSceneResidency;
        uploadDesc.frameIndex = graphics.frameIndex;
        const uintptr_t sourceIdentity = reinterpret_cast<uintptr_t>(
            gMeshRendererState.gpuDrivenSceneSourceIdentity);
        const std::span<const uint32_t> materialBindings =
            gMeshRendererState.gpuMaterialRegistry.GetSourceBindings(
                sourceIdentity);
        uploadDesc.materialSlotBySourceRecord = materialBindings.data();
        uploadDesc.materialSourceRecordCount = materialBindings.size();
        uploadDesc.materialBindingVersion =
            gMeshRendererState.gpuMaterialRegistry.GetBindingVersion(
                sourceIdentity);
        const auto* massSource =
            gMeshRendererState.gpuDrivenSceneSource.mass;
        const uintptr_t massSourceIdentity =
            reinterpret_cast<uintptr_t>(massSource);
        const std::span<const uint32_t> massMaterialBindings =
            massSource != nullptr
                ? gMeshRendererState.gpuMaterialRegistry.GetSourceBindings(
                    massSourceIdentity)
                : std::span<const uint32_t>{};
        uploadDesc.massMaterialSlotBySourceRecord =
            massMaterialBindings.data();
        uploadDesc.massMaterialSourceRecordCount =
            massMaterialBindings.size();
        uploadDesc.massMaterialBindingVersion =
            massSource != nullptr
                ? gMeshRendererState.gpuMaterialRegistry.GetBindingVersion(
                    massSourceIdentity)
                : 0u;
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

        if (!gMeshRendererState.gpuMaterialRegistry.StageProgramBlocksFrame(
                gMeshRendererState.frameResources.GetActiveFrameIndex(),
                frame.materialProgramParameterMapped,
                frame.materialProgramResourceMapped,
                kMaxMaterialDataCount,
                gMeshRendererState.materialUploadRanges)) {
            return;
        }
        gMeshRendererState.frameResources.
            CommitActiveMaterialProgramBlockRanges(
                commandList,
                gMeshRendererState.materialUploadRanges);
    }

    void SyncGpuDrivenSceneMaterialSources(
        const GFX::Context& graphics,
        uintptr_t sourceIdentity,
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {
        const RENDER3D::MATERIAL::GpuMaterialSourceSyncMode syncMode =
            gMeshRendererState.gpuMaterialRegistry.BeginSourceSync(
                sourceIdentity,
                source.layoutVersion,
                source.sourceVersion,
                source.dirtyBaseSourceVersion,
                source.sourceRecordCount);
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
                    source.GetPass(passKind);
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
                    source.GetPass(passKind);
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

    void SyncSurfaceGpuSceneMaterialFrame(const GFX::Context& graphics) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::MaterialPrepare);
        SyncGpuDrivenSceneMaterialSources(
            graphics,
            reinterpret_cast<uintptr_t>(
                gMeshRendererState.gpuDrivenSceneSourceIdentity),
            gMeshRendererState.gpuDrivenSceneSource);
        const RENDER3D::GPUDRIVEN::MassGpuSceneSource* mass =
            gMeshRendererState.gpuDrivenSceneSource.mass;
        if (mass == nullptr) {
            return;
        }
        const uintptr_t sourceIdentity =
            reinterpret_cast<uintptr_t>(mass);
        const RENDER3D::MATERIAL::GpuMaterialSourceSyncMode syncMode =
            gMeshRendererState.gpuMaterialRegistry.BeginSourceSync(
                sourceIdentity,
                mass->layoutVersion,
                mass->materialVersion,
                mass->materialVersion,
                static_cast<uint32_t>(mass->materialSources.size()));
        if (syncMode ==
            RENDER3D::MATERIAL::GpuMaterialSourceSyncMode::None) {
            return;
        }
        MeshDrawContext drawCtx =
            BuildDrawContext(graphics, false, MeshDrawPassKind::Forward, {});
        (void)PrepareSurfaceGpuSceneMaterialSources(
            drawCtx,
            mass->materialSources.data(),
            mass->materialSources.size());
        gMeshRendererState.gpuMaterialRegistry.EndSourceSync();
    }

    void UpdateTraditionalCommandStreamStats() {
        const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
            gMeshRendererState.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
        gMeshRendererState.debugStats.traditionalCommandStream = indirectStats;
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
