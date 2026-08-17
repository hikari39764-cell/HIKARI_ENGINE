#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

    namespace {

        constexpr size_t kMaxMaterialTextureGpuLoadsPerFrame = 2;

    } // namespace
    MeshFrameResources& GetActiveMeshFrameResources() {
        return gMeshRendererState.frameResources.Active();
    }

    MATH::Mat4 ResolveGpuDrivenCullingViewProj() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.cullingCameraMapped != nullptr
            ? frame.cullingCameraMapped->viewProj
            : MATH::Mat4::Identity();
    }

    MATH::Vec3 ResolveGpuDrivenCullingCameraPosition() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.cullingCameraMapped != nullptr
            ? MATH::Vec3{
                frame.cullingCameraMapped->cameraPos.x,
                frame.cullingCameraMapped->cameraPos.y,
                frame.cullingCameraMapped->cameraPos.z
            }
            : MATH::Vec3{};
    }

    D3D12_GPU_VIRTUAL_ADDRESS ResolveCameraAddress() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.cameraCB != nullptr
            ? frame.cameraCB->GetGPUVirtualAddress()
            : 0;
    }

    D3D12_GPU_VIRTUAL_ADDRESS ResolveCullingCameraAddress() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.cullingCameraCB != nullptr
            ? frame.cullingCameraCB->GetGPUVirtualAddress()
            : (frame.cameraCB != nullptr
                ? frame.cameraCB->GetGPUVirtualAddress()
                : 0);
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetSceneSourcePass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

        return gMeshRendererState.gpuDrivenSceneSource.GetPass(passKind);
    }

    bool HasGpuDrivenPassSource(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

        return GetSceneSourcePass(passKind).HasGpuSceneRange();
    }

    MeshRendererTraditionalIndirectHydrationContext
    BuildTraditionalIndirectHydrationContext(const GFX::Context& graphics) {

        MeshRendererTraditionalIndirectHydrationContext context{};
        context.device = graphics.device;
        context.primitiveCache = &gMeshRendererState.primitiveCache;
        context.debugStats = &gMeshRendererState.debugStats;
        context.jointPaletteMapped =
            GetActiveMeshFrameResources().jointPaletteMapped;
        context.jointPaletteBuffer =
            GetActiveMeshFrameResources().jointPaletteCB.Get();
        return context;
    }

    void UploadMeshShaderJointPalettes() {
        MeshFrameResources& frame = GetActiveMeshFrameResources();
        const auto* palettes =
            gMeshRendererState.gpuDrivenSceneSource.meshShaderJointPalettes;
        if (frame.jointPaletteMapped == nullptr || palettes == nullptr) {
            return;
        }

        const size_t paletteCount =
            (std::min)(palettes->size(), static_cast<size_t>(kMaxObjectCount));
        for (size_t paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
            const std::vector<MATH::Mat4>& palette = (*palettes)[paletteIndex];
            if (palette.empty()) {
                continue;
            }
            (void)UploadJointPalette(
                frame.jointPaletteMapped,
                paletteIndex,
                palette);
        }
    }


    bool PrepareMeshFrame(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        RenderDebugView debugView,
        uint32_t overrideScreenWidth = 0,
        uint32_t overrideScreenHeight = 0,
        const MeshFrameCameraOverrides* cameraOverrides = nullptr) {
        MeshFrameResources& frameResources = GetActiveMeshFrameResources();
        if (frameResources.cameraMapped == nullptr ||
            frameResources.cullingCameraMapped == nullptr ||
            frameResources.lightMapped == nullptr ||
            frameResources.shadowMapped == nullptr ||
            frameResources.skyEnvironmentMapped == nullptr) {
            return false;
        }

        frameResources.cameraMapped->viewProj =
            cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                ? *cameraOverrides->renderViewProj
                : camera.GetViewProj();
        frameResources.cameraMapped->invViewProj =
            cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                ? *cameraOverrides->renderInvViewProj
                : MATH::Inverse(frameResources.cameraMapped->viewProj);
        const MATH::Vec3 cameraPos = camera.GetPosition();
        frameResources.cameraMapped->cameraPos = {
            cameraPos.x,
            cameraPos.y,
            cameraPos.z,
            1.0f
        };
        const FrameContext& frame = TIME::GetFrameContext();
        frameResources.cameraMapped->timeParams = {
            TIME::GetUnscaledElapsedSeconds(),
            frame.unscaledDt,
            frame.gameDt,
            static_cast<float>(frame.frameIndex)
        };
        frameResources.cameraMapped->simulationTimeParams = {
            TIME::GetInterpolatedSimulationTimeSeconds(),
            frame.fixedDt,
            frame.fixedInterpolationAlpha,
            static_cast<float>(frame.fixedTickIndex)
        };
        int screenW = static_cast<int>(overrideScreenWidth);
        int screenH = static_cast<int>(overrideScreenHeight);
        if (screenW <= 0 || screenH <= 0) {
            screenW = std::max(1, graphics.backBufferWidth);
            screenH = std::max(1, graphics.backBufferHeight);
        }
        frameResources.cameraMapped->screenParams = {
            static_cast<float>(screenW),
            static_cast<float>(screenH),
            1.0f / static_cast<float>(screenW),
            1.0f / static_cast<float>(screenH)
        };
        CameraCB cullingCamera = *frameResources.cameraMapped;
        if (cameraOverrides != nullptr && cameraOverrides->HasCullingMatrices()) {
            cullingCamera.viewProj = *cameraOverrides->cullingViewProj;
            cullingCamera.invViewProj = *cameraOverrides->cullingInvViewProj;
        }
        const uint32_t cullingWidth = static_cast<uint32_t>(screenW);
        const uint32_t cullingHeight = static_cast<uint32_t>(screenH);
        if (gMeshRendererState.freezeGpuDrivenCullingCamera) {
            const bool sizeChanged =
                gMeshRendererState.frozenCullingCameraWidth != cullingWidth ||
                gMeshRendererState.frozenCullingCameraHeight != cullingHeight;
            if (!gMeshRendererState.frozenCullingCameraValid || sizeChanged) {
                gMeshRendererState.frozenCullingCamera = cullingCamera;
                gMeshRendererState.frozenCullingCameraValid = true;
                gMeshRendererState.frozenCullingCameraWidth = cullingWidth;
                gMeshRendererState.frozenCullingCameraHeight = cullingHeight;
            }
            *frameResources.cullingCameraMapped = gMeshRendererState.frozenCullingCamera;
        } else {
            gMeshRendererState.frozenCullingCameraValid = false;
            gMeshRendererState.frozenCullingCameraWidth = 0;
            gMeshRendererState.frozenCullingCameraHeight = 0;
            *frameResources.cullingCameraMapped = cullingCamera;
        }
        gMeshRendererState.debugStats.gpuDrivenCullingCameraFrozen =
            gMeshRendererState.freezeGpuDrivenCullingCamera && gMeshRendererState.frozenCullingCameraValid;

        frameResources.localLightListView =
            RENDER3D::LIGHTING::BuildLocalLightList(
                lighting,
                camera,
                static_cast<uint32_t>(screenW),
                static_cast<uint32_t>(screenH),
                {
                    frameResources.localLightMapped,
                    RENDER3D::LIGHTING::kMaximumLocalLightCount,
                    frameResources.localLightClusterHeaderMapped,
                    RENDER3D::LIGHTING::
                        kMaximumLocalLightClusterCount,
                    frameResources.localLightClusterIndexMapped,
                    RENDER3D::LIGHTING::
                        kMaximumLocalLightClusterIndexCount
                });
        FillLightCB(
            environment,
            frameResources.localLightListView,
            debugView,
            *frameResources.lightMapped,
            gMeshRendererState.debugStats);
        FillShadowCB(
            environment,
            frame.frameIndex,
            *frameResources.shadowMapped);
        FillSkyEnvironmentCB(
            environment,
            cameraPos,
            camera.GetTarget(),
            *frameResources.skyEnvironmentMapped);
        return true;
    }

    MeshDrawContext BuildDrawContext(
        const GFX::Context& graphics,
        bool depthAwarePhase,
        MeshDrawPassKind passKind,
        const MeshPassResources& passResources
#if defined(HIKARI_WITH_EDITOR)
        , const MeshFrameBindingOverrides* overrides
#endif
    ) {
        MeshDrawContext ctx{};
        MeshFrameResources& frame = GetActiveMeshFrameResources();
        ctx.cmd = graphics.cmdList;
        ctx.staticRootSig = GetStaticRootSignature(gMeshRendererState.pipelines);
        ctx.skinnedRootSig = GetSkinnedRootSignature(gMeshRendererState.pipelines);
        ctx.objectCB = frame.objectCB.Get();
        ctx.objectDataBuffer = frame.objectDataBuffer.Get();
        ctx.materialDataBuffer = frame.materialDataBuffer.Get();
        ctx.jointPaletteCB = frame.jointPaletteCB.Get();
        ctx.objectMapped = frame.objectMapped;
        ctx.objectDataMapped = frame.objectDataMapped;
        ctx.materialDataMapped = frame.materialDataMapped;
        ctx.materialProgramParametersAddress =
            frame.materialProgramParameterBuffer
                ? frame.materialProgramParameterBuffer->GetGPUVirtualAddress()
                : 0u;
        ctx.materialProgramResourcesAddress =
            frame.materialProgramResourceBuffer
                ? frame.materialProgramResourceBuffer->GetGPUVirtualAddress()
                : 0u;
        RENDER3D::WATER::WaterSurfaceGpuFrameBuffer* waterSurfaceBuffer =
            &gMeshRendererState.waterSurfaceGpuFrameBuffer;
        ctx.jointPaletteMapped = frame.jointPaletteMapped;
        ctx.gpuMaterialRegistry = &gMeshRendererState.gpuMaterialRegistry;
        ctx.objectDataSrv = frame.objectDataSrvGpu;
        ctx.materialDataSrv = frame.materialDataSrvGpu;
#if defined(HIKARI_WITH_EDITOR)
        ctx.surfaceGpuSceneFrameBuffer =
            overrides != nullptr &&
                overrides->surfaceGpuSceneFrameBuffer != nullptr
                ? overrides->surfaceGpuSceneFrameBuffer
                : &gMeshRendererState.surfaceGpuSceneBuffer;
        ctx.surfaceGpuSceneSrv =
            ctx.surfaceGpuSceneFrameBuffer->GetSrv();
        if (overrides != nullptr &&
            overrides->waterSurfaceGpuFrameBuffer != nullptr) {
            waterSurfaceBuffer = overrides->waterSurfaceGpuFrameBuffer;
        }
        ctx.traditionalCommandStreamBuffer =
            overrides != nullptr &&
                overrides->traditionalCommandStreamBuffer != nullptr
                ? overrides->traditionalCommandStreamBuffer
                : &gMeshRendererState.traditionalCommandStreamBuffer;
        ctx.cameraAddress =
            overrides != nullptr && overrides->cameraAddress != 0
                ? overrides->cameraAddress
                : ResolveCameraAddress();
        ctx.cullingCameraAddress =
            overrides != nullptr && overrides->cullingCameraAddress != 0
                ? overrides->cullingCameraAddress
                : ResolveCullingCameraAddress();
        ctx.lightAddress =
            overrides != nullptr && overrides->lightAddress != 0
                ? overrides->lightAddress
                : (frame.lightCB
                    ? frame.lightCB->GetGPUVirtualAddress()
                    : 0);
        ctx.localLightsAddress =
            overrides != nullptr &&
                overrides->localLightsAddress != 0
                ? overrides->localLightsAddress
                : (frame.localLightBuffer
                    ? frame.localLightBuffer->
                        GetGPUVirtualAddress()
                    : 0);
        ctx.localLightClusterHeadersAddress =
            overrides != nullptr &&
                overrides->localLightClusterHeadersAddress != 0
                ? overrides->localLightClusterHeadersAddress
                : (frame.localLightClusterHeaderBuffer
                    ? frame.localLightClusterHeaderBuffer->
                        GetGPUVirtualAddress()
                    : 0);
        ctx.localLightClusterIndicesAddress =
            overrides != nullptr &&
                overrides->localLightClusterIndicesAddress != 0
                ? overrides->localLightClusterIndicesAddress
                : (frame.localLightClusterIndexBuffer
                    ? frame.localLightClusterIndexBuffer->
                        GetGPUVirtualAddress()
                    : 0);
        ctx.shadowAddress =
            overrides != nullptr && overrides->shadowAddress != 0
                ? overrides->shadowAddress
                : (frame.shadowCB
                    ? frame.shadowCB->GetGPUVirtualAddress()
                    : 0);
        ctx.skyEnvironmentAddress =
            overrides != nullptr &&
                overrides->skyEnvironmentAddress != 0
                ? overrides->skyEnvironmentAddress
                : (frame.skyEnvironmentCB
                    ? frame.skyEnvironmentCB->GetGPUVirtualAddress()
                    : 0);
#else
        ctx.surfaceGpuSceneSrv = gMeshRendererState.surfaceGpuSceneBuffer.GetSrv();
        ctx.surfaceGpuSceneFrameBuffer = &gMeshRendererState.surfaceGpuSceneBuffer;
        ctx.traditionalCommandStreamBuffer = &gMeshRendererState.traditionalCommandStreamBuffer;
        ctx.cameraAddress = ResolveCameraAddress();
        ctx.cullingCameraAddress = ResolveCullingCameraAddress();
        ctx.lightAddress = frame.lightCB
            ? frame.lightCB->GetGPUVirtualAddress()
            : 0;
        ctx.localLightsAddress = frame.localLightBuffer
            ? frame.localLightBuffer->GetGPUVirtualAddress()
            : 0;
        ctx.localLightClusterHeadersAddress =
            frame.localLightClusterHeaderBuffer
                ? frame.localLightClusterHeaderBuffer->
                    GetGPUVirtualAddress()
                : 0;
        ctx.localLightClusterIndicesAddress =
            frame.localLightClusterIndexBuffer
                ? frame.localLightClusterIndexBuffer->
                    GetGPUVirtualAddress()
                : 0;
        ctx.shadowAddress = frame.shadowCB
            ? frame.shadowCB->GetGPUVirtualAddress()
            : 0;
        ctx.skyEnvironmentAddress = frame.skyEnvironmentCB
            ? frame.skyEnvironmentCB->GetGPUVirtualAddress()
            : 0;
#endif
        ctx.surfaceFeatureDataAddress =
            waterSurfaceBuffer != nullptr
                ? waterSurfaceBuffer->GetRecordGpuVirtualAddress()
                : 0u;
        ctx.sandSurfaceDataAddress =
            gMeshRendererState.sandSurfaceGpuFrameBuffer.
                GetRecordGpuVirtualAddress();
        ctx.surfaceFeatureHeightsAddress =
            gMeshRendererState.sandSurfaceGpuFrameBuffer.
                GetHeightGpuVirtualAddress();
        ctx.passKind = passKind;
        ctx.binding.cmd = ctx.cmd;
        ctx.binding.graphics = &graphics;
        ctx.binding.depthAwarePhase = depthAwarePhase;
        ctx.binding.fallbackTextureHandle = gMeshRendererState.fallbackTextureHandle;
        ctx.binding.fallbackNormalTextureHandle = gMeshRendererState.fallbackNormalTextureHandle;
        ctx.binding.fallbackBlackTextureHandle = gMeshRendererState.fallbackBlackTextureHandle;
        ctx.binding.fallbackCubeTextureHandle = gMeshRendererState.fallbackCubeTextureHandle;
        ctx.binding.passResources = passResources;
        if (ctx.binding.passResources.fallbackAoTextureHandle < 0) {
            ctx.binding.passResources.fallbackAoTextureHandle = gMeshRendererState.fallbackTextureHandle;
        }
        ctx.materialFill.fallbackTextureHandle = gMeshRendererState.fallbackTextureHandle;
        ctx.materialFill.fallbackNormalTextureHandle = gMeshRendererState.fallbackNormalTextureHandle;
        ctx.materialFill.fallbackBlackTextureHandle = gMeshRendererState.fallbackBlackTextureHandle;
        ctx.materialFill.stats = &gMeshRendererState.debugStats;
        ctx.services.device = graphics.device;
        ctx.services.primitiveCache = &gMeshRendererState.primitiveCache;
        ctx.services.materialResolver = &gMeshRendererState.materialResolver;
        ctx.services.pipelines = &gMeshRendererState.pipelines;
        ctx.services.stats = &gMeshRendererState.debugStats;
        return ctx;
    }

    bool HasGpuDrivenSceneSource() {
        return gMeshRendererState.gpuDrivenSceneSource.HasAnyGpuSceneRanges();
    }


    bool BeginFrameInternal(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {

        if (!EnsureInitialized(graphics)) {
            return false;
        }

        gMeshRendererState.frameResources.Activate(graphics.frameIndex);
        gMeshRendererState.materialResolver.BeginFrame(kMaxMaterialTextureGpuLoadsPerFrame);
        gMeshRendererState.gpuMaterialRegistry.BeginFrame();
        // Capture 逕ｨ縺ｮ蝗ｺ螳夊ｧ｣蜒丞ｺｦ繧・camera constants 縺ｫ蜿肴丐縺吶ｋ縲・
        if (!PrepareMeshFrame(
                graphics,
                camera,
                environment,
                lighting,
                debugView,
                screenWidth,
                screenHeight,
                cameraOverrides)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        if (cmd == nullptr ||
            !gMeshRendererState.frameResources.HasActiveDrawResources()) {
            return false;
        }

        gMeshRendererState.frameObjectIndex = 0;
        SyncSurfaceGpuSceneMaterialFrame(graphics);
        if (!gMeshRendererState.particlePass.PrepareFrame(
                graphics,
                gMeshRendererState.particleFrameSource,
                gMeshRendererState.gpuMaterialRegistry)) {
            return false;
        }
        CommitActiveMaterialDataFrame(cmd);
        if (HasGpuDrivenSceneSource()) {
            UploadMeshShaderJointPalettes();
            gMeshRendererState.traditionalIndirectOwner.RefreshForActivePipeline(
                gMeshRendererState.gpuDrivenSceneSource,
                BuildTraditionalIndirectHydrationContext(graphics));
            PrepareGpuDrivenFrameState(graphics);
        } else {
            ResetGpuDrivenFrameState(graphics);
        }

        cmd->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* srvHeap =
            RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmd->SetDescriptorHeaps(1, heaps);
        }
        return true;
    }

} // namespace HIKARI::MESHRENDERER::INTERNAL
