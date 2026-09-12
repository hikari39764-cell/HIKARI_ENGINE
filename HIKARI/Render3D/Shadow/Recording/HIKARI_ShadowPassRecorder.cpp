#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <array>
#include <limits>

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"

namespace HIKARI::SHADOW::INTERNAL {

    void UploadShadowCameraConstants(
        const ShadowLightFrame& frame,
        uint32_t cascadeIndex) {
        ShadowCameraCB* mapped = GetActiveShadowFrameResources().cameraMapped;
        if (mapped == nullptr ||
            cascadeIndex >= kMaxDirectionalShadowCascades) {
            return;
        }
        ShadowCameraCB* camera = reinterpret_cast<ShadowCameraCB*>(
            reinterpret_cast<uint8_t*>(mapped) +
            static_cast<size_t>(ShadowCameraConstantStride()) * cascadeIndex);

        const FrameContext& frameContext = TIME::GetFrameContext();
        const float resolution =
            static_cast<float>((std::max)(1u, gShadowRendererState.resolution));
        camera->lightViewProj = frame.viewProj;
        camera->invLightViewProj = MATH::Inverse(frame.viewProj);
        camera->lightPosition = {
            frame.lightPosition.x,
            frame.lightPosition.y,
            frame.lightPosition.z,
            1.0f
        };
        camera->timeParams = {
            TIME::GetUnscaledElapsedSeconds(),
            frameContext.unscaledDt,
            frameContext.gameDt,
            static_cast<float>(frameContext.frameIndex)
        };
        camera->simulationTimeParams = {
            TIME::GetInterpolatedSimulationTimeSeconds(),
            frameContext.fixedDt,
            frameContext.fixedInterpolationAlpha,
            static_cast<float>(frameContext.fixedTickIndex)
        };
        camera->screenParams = {
            resolution,
            resolution,
            1.0f / resolution,
            1.0f / resolution
        };
    }

    void SubmitShadowDebugFrustum(const SceneEnvironment& environment, const Camera3D& camera) {
        if (!environment.directionalShadow.showDebugFrustum) {
            return;
        }
        (void)camera;
        constexpr uint32_t colors[] = {
            0xFF57D6FFu,
            0xFF65E58Au,
            0xFFFFC857u,
            0xFFFF6B8Au
        };
        for (uint32_t cascadeIndex = 0;
             cascadeIndex < gShadowRendererState.cascadeCount;
             ++cascadeIndex) {
            const ShadowLightFrame& frame =
                gShadowRendererState.cascadeFrames[cascadeIndex];
            const float half = frame.orthoSize * 0.5f;
            const MATH::Vec3 nearCenter =
                frame.lightPosition +
                frame.lightDirection * frame.lightNear;
            const MATH::Vec3 farCenter =
                frame.lightPosition +
                frame.lightDirection * frame.lightFar;
            const std::array<MATH::Vec3, 8> corners = {
                nearCenter - frame.right * half - frame.up * half,
                nearCenter + frame.right * half - frame.up * half,
                nearCenter + frame.right * half + frame.up * half,
                nearCenter - frame.right * half + frame.up * half,
                farCenter - frame.right * half - frame.up * half,
                farCenter + frame.right * half - frame.up * half,
                farCenter + frame.right * half + frame.up * half,
                farCenter - frame.right * half + frame.up * half,
            };
            const auto submit = [&](int a, int b) {
                RENDERER3D::DEBUG::SubmitStroke3D({
                    corners[static_cast<size_t>(a)],
                    corners[static_cast<size_t>(b)],
                    colors[cascadeIndex],
                    RENDERER3D::DEBUG::DebugDepthMode::XRay,
                    1.8f
                });
            };
            submit(0, 1); submit(1, 2); submit(2, 3); submit(3, 0);
            submit(4, 5); submit(5, 6); submit(6, 7); submit(7, 4);
            submit(0, 4); submit(1, 5); submit(2, 6); submit(3, 7);
        }
    }
    void ClearShadowFrameSubmissions() {
        gShadowRendererState.debugStats = {};
        gShadowRendererState.frameHasShadowWork = false;
        gShadowRendererState.frameHasStaticShadowWork = false;
        gShadowRendererState.frameHasDynamicShadowWork = false;
        gShadowRendererState.shadowCache.BeginFrame();
        PublishShadowCacheStats();
    }

    bool UploadShadowGpuSceneFrame(
        const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        const bool hasShadowWork = source.HasAnyGpuSceneRanges();
        gShadowRendererState.gpuDrivenLayer.BeginFrame(
            hasShadowWork
                ? &source
                : nullptr);

        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
        uploadDesc.commandList = graphics.cmdList;
        uploadDesc.frameIndex = graphics.frameIndex;
        uploadDesc.allowDirtyRangePatching = true;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            gShadowRendererState.gpuDrivenLayer.UploadSceneFrame(uploadDesc);
        const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
            uploadStats.bufferStats;
        gShadowRendererState.debugStats.shadowGpuSceneCapacity = gpuSceneStats.capacity;
        gShadowRendererState.debugStats.shadowGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
        gShadowRendererState.debugStats.shadowGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
        gShadowRendererState.debugStats.shadowGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
        gShadowRendererState.debugStats.shadowGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
        gShadowRendererState.debugStats.shadowGpuSceneFullUploadCount =
            uploadStats.uploadedFullScene ? 1u : 0u;
        gShadowRendererState.debugStats.shadowGpuSceneDirtyPatchCount =
            uploadStats.patchedDirtyRanges ? 1u : 0u;
        gShadowRendererState.debugStats.shadowGpuSceneReuseCount =
            uploadStats.reusedResidentFrame ? 1u : 0u;
        gShadowRendererState.debugStats.shadowGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
        gShadowRendererState.debugStats.shadowGpuSceneBufferReady = gpuSceneStats.initialized;
        return hasShadowWork;
    }

    bool PrepareShadowSourceForDraw(
        const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        if (!source.HasAnyGpuSceneRanges()) {
            ResetShadowGpuDrivenWorkFrame(graphics);
            return false;
        }

        // The built sources belong to the source cache.  A draw may select a
        // split source, but it must never replace the combined source used by
        // later frames and by the correctness fallback.
        gShadowRendererState.activeShadowSceneSource = source;
        ResetShadowMaterialFrame();
        if (!UploadShadowGpuSceneFrame(
                graphics,
                gShadowRendererState.activeShadowSceneSource)) {
            return false;
        }
        PrepareShadowSurfaceGpuSceneMaterialFrame();
        CommitShadowMaterialDataFrame(graphics.cmdList);
        gShadowRendererState.gpuDrivenLayer.CommitSurfaceGpuSceneMaterialFrame(
            graphics.cmdList);
        return true;
    }

    void ResetShadowGpuDrivenWorkFrame(const GFX::Context& graphics) {
        gShadowRendererState.gpuDrivenFrame.Reset();
        gShadowRendererState.clusterGpuDrivenProducer.BeginFrame(false);
        gShadowRendererState.gpuDrivenLayer.ImportProducerOutput(
            gShadowRendererState.clusterGpuDrivenProducer.BuildFrameOutput());
        gShadowRendererState.meshletRenderBackend.ResetFrame();
        SyncShadowGpuDrivenBackendAvailability();
        UploadShadowIndirectDrawFrame(graphics);
    }

    void BuildShadowGpuDrivenWorkFrame(const GFX::Context& graphics) {
        const RENDER3D::GPUDRIVEN::GpuDrivenFrameBuildInput input =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(
                gShadowRendererState.activeShadowSceneSource);
        gShadowRendererState.gpuDrivenFrame =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);

        if (!gShadowRendererState.activeShadowSceneSource.HasAnyGpuSceneRanges() ||
            !gShadowRendererState.surfaceGpuSceneBuffer.GetStats().initialized ||
            gShadowRendererState.surfaceGpuSceneBuffer.GetStats().overflowInstanceCount != 0) {
            ResetShadowGpuDrivenWorkFrame(graphics);
            return;
        }

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (graphics.cmdList != nullptr && srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            graphics.cmdList->SetDescriptorHeaps(1, heaps);
        }

        gShadowRendererState.clusterGpuDrivenProducer.BeginFrame(false);
        RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
        workContext.producer = &gShadowRendererState.clusterGpuDrivenProducer;
        workContext.commandList = graphics.cmdList;
        workContext.viewProj = gShadowRendererState.lightViewProj;
        workContext.cameraPosition = gShadowRendererState.lightCullPosition;
        workContext.geometryPoolSrv =
            RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
        workContext.surfaceGpuSceneGpuAddress =
            gShadowRendererState.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
        workContext.frame = &gShadowRendererState.gpuDrivenFrame;
        (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);

        gShadowRendererState.gpuDrivenLayer.ImportProducerOutput(
            gShadowRendererState.clusterGpuDrivenProducer.BuildFrameOutput());
        gShadowRendererState.gpuDrivenLayer.BuildCommandBuffers();
        gShadowRendererState.meshletRenderBackend.ResetFrame();
        SyncShadowGpuDrivenBackendAvailability();
        UploadShadowIndirectDrawFrame(graphics);
    }

    void UploadShadowIndirectDrawFrame(const GFX::Context& graphics) {
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
        commandFrameDesc.commandList = graphics.cmdList;
        commandFrameDesc.cullViewProj = &gShadowRendererState.lightViewProj;
        commandFrameDesc.frameIndex = graphics.frameIndex;
        gShadowRendererState.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);

        const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
            gShadowRendererState.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
        gShadowRendererState.debugStats.shadowIndirectCapacity = indirectStats.capacity;
        gShadowRendererState.debugStats.shadowIndirectRequestedCommandCount = indirectStats.requestedCommandCount;
        gShadowRendererState.debugStats.shadowIndirectUploadedCommandCount = indirectStats.uploadedCommandCount;
        gShadowRendererState.debugStats.shadowIndirectOverflowCommandCount = indirectStats.overflowCommandCount;
        gShadowRendererState.debugStats.shadowIndirectMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
        gShadowRendererState.debugStats.shadowIndirectArgumentBufferReady = indirectStats.initialized;
        gShadowRendererState.debugStats.shadowIndirectCommandSignatureReady = indirectStats.commandSignatureReady;
    }

    bool ExecuteShadowGpuDrivenBackend(
        const GFX::Context& graphics,
        RENDER3D::GPUDRIVEN::GeometryBackendKind backend,
        GFX::GPU_PROFILE::Pass traditionalProfilePass,
        GFX::GPU_PROFILE::Pass meshletProfilePass) {

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        if (cmd == nullptr) {
            return false;
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenPassKind shadowPass =
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow;
        const RENDER3D::GPUDRIVEN::GeometryBackendContext backendContext =
            gShadowRendererState.gpuDrivenLayer.BuildGeometryBackendContext(
                cmd,
                shadowPass,
                backend);

        if (backend == RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs) {
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
            if ((!hasStaticStream && !hasSkinnedStream) || !gShadowRendererState.depthPipelines.IsReady()) {
                return false;
            }

            const auto* bucketVariants = range->traditionalIndirect != nullptr
                ? range->traditionalIndirect->bucketVariants : nullptr;
            if (bucketVariants == nullptr || bucketVariants->empty()) return false;
            const size_t executableBucketCount = (std::min)(commandBucketCount, bucketVariants->size());
            bool executed = false;
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

            GFX::GPU_PROFILE::ScopedGpuTimer gpuDraw(
                cmd,
                traditionalProfilePass);
            if (hasStaticStream) {
                BindShadowGpuDrivenFrameResources(
                    graphics,
                    cmd,
                    nullptr);
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    cmd->SetPipelineState(gShadowRendererState.depthPipelines.Resolve(
                        false, (*bucketVariants)[bucketIndex].doubleSided));
                    cmd->ExecuteIndirect(
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
                }
                executed = true;
            }

            if (hasSkinnedStream) {
                BindShadowGpuDrivenFrameResources(
                    graphics,
                    cmd,
                    nullptr,
                    nullptr,
                    true);
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    cmd->SetPipelineState(gShadowRendererState.depthPipelines.Resolve(
                        true, (*bucketVariants)[bucketIndex].doubleSided));
                    cmd->ExecuteIndirect(
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
                }
                executed = true;
            }

            gShadowRendererState.debugStats.submittedCasterCount += range->commandCount;
            gShadowRendererState.debugStats.staticCasterDrawCount += range->staticCommandCount;
            gShadowRendererState.debugStats.skinnedCasterDrawCount += range->skinnedCommandCount;
            gShadowRendererState.debugStats.totalPrimitiveCasterDrawCount += range->commandCount;
            return executed;
        }

        ID3D12Resource* visibleRangeBuffer =
            backendContext.visibility != nullptr
                ? backendContext.visibility->visibleMeshletRangeBuffer
                : nullptr;
        ID3D12Resource* visibleClusterListBuffer =
            backendContext.visibility != nullptr
                ? backendContext.visibility->visibleMeshletClusterListBuffer
                : nullptr;
        BindShadowGpuDrivenFrameResources(
            graphics,
            cmd,
            visibleRangeBuffer,
            visibleClusterListBuffer);

        switch (backend) {
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader: {
            if (visibleRangeBuffer == nullptr ||
                visibleClusterListBuffer == nullptr) {
                return false;
            }
            RENDER3D::MESHLET::MeshletRenderExecutionContext ctx{};
            ctx.commandList = backendContext.commandList;
            ctx.pass = backendContext.pass;
            ctx.visibility = backendContext.visibility;
            ctx.drawCommandRange = backendContext.drawCommandRange;
            ctx.pipelineKind = RENDER3D::MESHLET::MeshletPipelineKind::Shadow;
            ctx.profilePass = meshletProfilePass;
            const RENDER3D::MESHLET::MeshletRenderBackendStats beforeStats =
                gShadowRendererState.meshletRenderBackend.GetStats();
            const bool executed = gShadowRendererState.meshletRenderBackend.Execute(ctx);
            const RENDER3D::MESHLET::MeshletRenderBackendStats afterStats =
                gShadowRendererState.meshletRenderBackend.GetStats();
            AccumulateShadowMeshletStatsDelta(beforeStats, afterStats);
            return executed;
        }
        default:
            return false;
        }
    }

    bool ExecuteShadowGpuDrivenPass(
        const GFX::Context& graphics,
        GFX::GPU_PROFILE::Pass traditionalProfilePass,
        GFX::GPU_PROFILE::Pass meshletProfilePass) {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassKind shadowPass =
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow;
        SyncShadowGpuDrivenBackendAvailability();
        if (!gShadowRendererState.gpuDrivenLayer.IsPassGpuReady(shadowPass)) {
            return false;
        }

        const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
            gShadowRendererState.gpuDrivenLayer.GetPassExecutionPlan(shadowPass);
        bool executed = false;
        for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
            if (ExecuteShadowGpuDrivenBackend(
                    graphics,
                    plan.gpuBackends[i],
                    traditionalProfilePass,
                    meshletProfilePass)) {
                executed = true;
            }
        }
        return executed;
    }

} // namespace HIKARI::SHADOW::INTERNAL
