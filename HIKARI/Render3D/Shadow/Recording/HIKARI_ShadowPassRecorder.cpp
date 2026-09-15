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

    bool UploadShadowCameraConstants(
        ShadowRendererState& state, const GFX::Context& graphics,
        const ShadowLightFrame& frame,
        uint32_t cascadeIndex) {
        if (cascadeIndex >= kMaxDirectionalShadowCascades) return false;
        auto& resources = GetActiveShadowFrameResources(state);
        resources.cameraAddresses[cascadeIndex] = 0;
        // 同一フレームの別 View が先に記録したカメラを上書きしない。
        resources.frameUploads.ResetForFrame(graphics.currentFrameRetireFenceValue);
        const auto upload = resources.frameUploads.Allocate(sizeof(ShadowCameraCB));
        if (!upload) return false;
        auto* camera = static_cast<ShadowCameraCB*>(upload.cpuAddress);

        const FrameContext& frameContext = TIME::GetFrameContext();
        const float resolution =
            static_cast<float>((std::max)(1u, state.resolution));
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
        resources.cameraAddresses[cascadeIndex] = upload.gpuAddress;
        return true;
    }

    void SubmitShadowDebugFrustum(ShadowRendererState& state, const SceneEnvironment& environment, const Camera3D& camera) {
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
             cascadeIndex < state.cascadeCount;
             ++cascadeIndex) {
            const ShadowLightFrame& frame =
                state.cascadeFrames[cascadeIndex];
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
    void ClearShadowFrameSubmissions(ShadowRendererState& state) {
        state.debugStats = {};
        state.frameHasShadowWork = false;
        state.frameHasStaticShadowWork = false;
        state.frameHasDynamicShadowWork = false;
        state.shadowCache.BeginFrame();
        PublishShadowCacheStats(state);
    }

    bool UploadShadowGpuSceneFrame(
        ShadowRendererState& state, const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        const bool hasShadowWork = source.HasAnyGpuSceneRanges();
        state.gpuDrivenLayer.BeginFrame(
            hasShadowWork
                ? &source
                : nullptr);

        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDesc{};
        uploadDesc.commandList = graphics.cmdList;
        uploadDesc.frameIndex = graphics.frameIndex;
        uploadDesc.allowDirtyRangePatching = true;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            state.gpuDrivenLayer.UploadSceneFrame(uploadDesc);
        const RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBufferStats& gpuSceneStats =
            uploadStats.bufferStats;
        state.debugStats.shadowGpuSceneCapacity = gpuSceneStats.capacity;
        state.debugStats.shadowGpuSceneRequestedInstanceCount = gpuSceneStats.requestedInstanceCount;
        state.debugStats.shadowGpuSceneUploadedInstanceCount = gpuSceneStats.uploadedInstanceCount;
        state.debugStats.shadowGpuSceneOverflowInstanceCount = gpuSceneStats.overflowInstanceCount;
        state.debugStats.shadowGpuSceneUploadCallCount = gpuSceneStats.uploadCallCount;
        state.debugStats.shadowGpuSceneFullUploadCount =
            uploadStats.uploadedFullScene ? 1u : 0u;
        state.debugStats.shadowGpuSceneDirtyPatchCount =
            uploadStats.patchedDirtyRanges ? 1u : 0u;
        state.debugStats.shadowGpuSceneReuseCount =
            uploadStats.reusedResidentFrame ? 1u : 0u;
        state.debugStats.shadowGpuSceneSrvValid = gpuSceneStats.srv.ptr != 0;
        state.debugStats.shadowGpuSceneBufferReady = gpuSceneStats.initialized;
        return hasShadowWork;
    }

    bool PrepareShadowSourceForDraw(
        ShadowRendererState& state, const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source) {

        if (!source.HasAnyGpuSceneRanges()) {
            ResetShadowGpuDrivenWorkFrame(state, graphics);
            return false;
        }

        // The built sources belong to the source cache.  A draw may select a
        // split source, but it must never replace the combined source used by
        // later frames and by the correctness fallback.
        state.activeShadowSceneSource = source;
        ResetShadowMaterialFrame(state);
        if (!UploadShadowGpuSceneFrame(
                state, graphics,
                state.activeShadowSceneSource)) {
            return false;
        }
        PrepareShadowSurfaceGpuSceneMaterialFrame(state);
        return CommitShadowMaterialDataFrame(state, graphics) &&
            state.gpuDrivenLayer.CommitSurfaceGpuSceneMaterialFrame(graphics.cmdList);
    }

    void ResetShadowGpuDrivenWorkFrame(ShadowRendererState& state, const GFX::Context& graphics) {
        state.gpuDrivenFrame.Reset();
        state.clusterGpuDrivenProducer.BeginFrame(false);
        state.gpuDrivenLayer.ImportProducerOutput(
            state.clusterGpuDrivenProducer.BuildFrameOutput());
        state.meshletRenderBackend.ResetFrame();
        SyncShadowGpuDrivenBackendAvailability(state);
        UploadShadowIndirectDrawFrame(state, graphics);
    }

    void BuildShadowGpuDrivenWorkFrame(ShadowRendererState& state, const GFX::Context& graphics) {
        const RENDER3D::GPUDRIVEN::GpuDrivenFrameBuildInput input =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(
                state.activeShadowSceneSource);
        state.gpuDrivenFrame =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(input);

        if (!state.activeShadowSceneSource.HasAnyGpuSceneRanges() ||
            !state.surfaceGpuSceneBuffer.GetStats().initialized ||
            state.surfaceGpuSceneBuffer.GetStats().overflowInstanceCount != 0) {
            ResetShadowGpuDrivenWorkFrame(state, graphics);
            return;
        }

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (graphics.cmdList != nullptr && srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            graphics.cmdList->SetDescriptorHeaps(1, heaps);
        }

        state.clusterGpuDrivenProducer.BeginFrame(false);
        RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
        workContext.producer = &state.clusterGpuDrivenProducer;
        workContext.commandList = graphics.cmdList;
        workContext.viewProj = state.lightViewProj;
        workContext.cameraPosition = state.lightCullPosition;
        workContext.geometryPoolSrv =
            RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
        workContext.surfaceGpuSceneGpuAddress =
            state.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
        workContext.frame = &state.gpuDrivenFrame;
        (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);

        state.gpuDrivenLayer.ImportProducerOutput(
            state.clusterGpuDrivenProducer.BuildFrameOutput());
        state.gpuDrivenLayer.BuildCommandBuffers();
        state.meshletRenderBackend.ResetFrame();
        SyncShadowGpuDrivenBackendAvailability(state);
        UploadShadowIndirectDrawFrame(state, graphics);
    }

    void UploadShadowIndirectDrawFrame(ShadowRendererState& state, const GFX::Context& graphics) {
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDesc{};
        commandFrameDesc.commandList = graphics.cmdList;
        commandFrameDesc.cullViewProj = &state.lightViewProj;
        commandFrameDesc.frameIndex = graphics.frameIndex;
        state.gpuDrivenLayer.BuildCommandFrame(commandFrameDesc);

        const RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamStats& indirectStats =
            state.gpuDrivenLayer.GetCommandFrameStats().traditionalCommandStreamStats;
        state.debugStats.shadowIndirectCapacity = indirectStats.capacity;
        state.debugStats.shadowIndirectRequestedCommandCount = indirectStats.requestedCommandCount;
        state.debugStats.shadowIndirectUploadedCommandCount = indirectStats.uploadedCommandCount;
        state.debugStats.shadowIndirectOverflowCommandCount = indirectStats.overflowCommandCount;
        state.debugStats.shadowIndirectMissingDrawArgsCommandCount = indirectStats.missingDrawArgsCommandCount;
        state.debugStats.shadowIndirectArgumentBufferReady = indirectStats.initialized;
        state.debugStats.shadowIndirectCommandSignatureReady = indirectStats.commandSignatureReady;
    }

    bool ExecuteShadowGpuDrivenBackend(
        ShadowRendererState& state, const GFX::Context& graphics,
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
            state.gpuDrivenLayer.BuildGeometryBackendContext(
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
            if ((!hasStaticStream && !hasSkinnedStream) || !state.depthPipelines.IsReady()) {
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
                    state, graphics,
                    cmd,
                    nullptr);
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    cmd->SetPipelineState(state.depthPipelines.Resolve(
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
                    state, graphics,
                    cmd,
                    nullptr,
                    true);
                for (size_t bucketIndex = 0; bucketIndex < executableBucketCount; ++bucketIndex) {
                    cmd->SetPipelineState(state.depthPipelines.Resolve(
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

            state.debugStats.submittedCasterCount += range->commandCount;
            state.debugStats.staticCasterDrawCount += range->staticCommandCount;
            state.debugStats.skinnedCasterDrawCount += range->skinnedCommandCount;
            state.debugStats.totalPrimitiveCasterDrawCount += range->commandCount;
            return executed;
        }

        ID3D12Resource* visibleRangeBuffer =
            backendContext.visibility != nullptr
                ? backendContext.visibility->visibleMeshletRangeBuffer
                : nullptr;
        BindShadowGpuDrivenFrameResources(
            state, graphics,
            cmd,
            visibleRangeBuffer);

        switch (backend) {
        case RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader: {
            if (visibleRangeBuffer == nullptr) {
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
                state.meshletRenderBackend.GetStats();
            const bool executed = state.meshletRenderBackend.Execute(ctx);
            const RENDER3D::MESHLET::MeshletRenderBackendStats afterStats =
                state.meshletRenderBackend.GetStats();
            AccumulateShadowMeshletStatsDelta(state, beforeStats, afterStats);
            return executed;
        }
        default:
            return false;
        }
    }

    bool ExecuteShadowGpuDrivenPass(
        ShadowRendererState& state, const GFX::Context& graphics,
        GFX::GPU_PROFILE::Pass traditionalProfilePass,
        GFX::GPU_PROFILE::Pass meshletProfilePass) {
        const RENDER3D::GPUDRIVEN::GpuDrivenPassKind shadowPass =
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow;
        SyncShadowGpuDrivenBackendAvailability(state);
        if (!state.gpuDrivenLayer.IsPassGpuReady(shadowPass)) {
            return false;
        }

        const RENDER3D::GPUDRIVEN::GeometryBackendExecutionPlan plan =
            state.gpuDrivenLayer.GetPassExecutionPlan(shadowPass);
        bool executed = false;
        for (size_t i = 0; i < plan.gpuBackendCount; ++i) {
            if (ExecuteShadowGpuDrivenBackend(
                    state, graphics,
                    plan.gpuBackends[i],
                    traditionalProfilePass,
                    meshletProfilePass)) {
                executed = true;
            }
        }
        return executed;
    }

    bool RecordShadowView(ShadowRendererState& state, const GFX::Context& graphics,
        const ShadowLightFrame& frame, const D3D12_RECT& region, bool clearDepth,
        GFX::GPU_PROFILE::Pass traditionalProfilePass, GFX::GPU_PROFILE::Pass meshletProfilePass) {
        if (!UploadShadowCameraConstants(state, graphics, frame, 0u) ||
            !PrepareShadowMapDepthRegion(state, graphics.cmdList, region, clearDepth)) return false;
        state.activeCascadeIndex = 0u;
        state.lightViewProj = frame.viewProj;
        state.lightCullPosition = frame.lightPosition;
        state.lightAnchor = frame.anchor;
        state.lightAnchorGrid = frame.anchorGrid;
        BuildShadowGpuDrivenWorkFrame(state, graphics);
        return ExecuteShadowGpuDrivenPass(state, graphics, traditionalProfilePass, meshletProfilePass);
    }

} // namespace HIKARI::SHADOW::INTERNAL
