#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#include <algorithm>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "HIKARI_Services.h"
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
    BuildTraditionalIndirectHydrationContext() {

        MeshRendererTraditionalIndirectHydrationContext context{};
        context.device = SERVICES::gCtx.device;
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
        const Camera3D& camera,
        const SceneEnvironment& environment,
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
        gMeshRendererState.elapsedTimeSec += std::max(0.0f, frame.unscaledDt);
        frameResources.cameraMapped->timeParams = {
            gMeshRendererState.elapsedTimeSec,
            frame.unscaledDt,
            frame.gameDt,
            static_cast<float>(frame.frameIndex)
        };
        int screenW = static_cast<int>(overrideScreenWidth);
        int screenH = static_cast<int>(overrideScreenHeight);
        if (screenW <= 0 || screenH <= 0) {
            screenW = std::max(1, SERVICES::gCtx.backBufferWidth);
            screenH = std::max(1, SERVICES::gCtx.backBufferHeight);
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

        FillLightCB(
            environment,
            debugView,
            *frameResources.lightMapped,
            gMeshRendererState.debugStats);
        FillShadowCB(environment, *frameResources.shadowMapped);
        FillSkyEnvironmentCB(
            environment,
            *frameResources.skyEnvironmentMapped);
        return true;
    }

    MeshDrawContext BuildDrawContext(
        bool depthAwarePhase,
        MeshDrawPassKind passKind,
        const MeshPassResources& passResources
#if defined(HIKARI_WITH_EDITOR)
        , const MeshFrameBindingOverrides* overrides
#endif
    ) {
        MeshDrawContext ctx{};
        MeshFrameResources& frame = GetActiveMeshFrameResources();
        ctx.cmd = SERVICES::gCtx.cmdList;
        ctx.staticRootSig = GetStaticRootSignature(gMeshRendererState.pipelines);
        ctx.skinnedRootSig = GetSkinnedRootSignature(gMeshRendererState.pipelines);
        ctx.objectCB = frame.objectCB.Get();
        ctx.objectDataBuffer = frame.objectDataBuffer.Get();
        ctx.materialDataBuffer = frame.materialDataBuffer.Get();
        ctx.jointPaletteCB = frame.jointPaletteCB.Get();
        ctx.objectMapped = frame.objectMapped;
        ctx.objectDataMapped = frame.objectDataMapped;
        ctx.materialDataMapped = frame.materialDataMapped;
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
        ctx.shadowAddress = frame.shadowCB
            ? frame.shadowCB->GetGPUVirtualAddress()
            : 0;
        ctx.skyEnvironmentAddress = frame.skyEnvironmentCB
            ? frame.skyEnvironmentCB->GetGPUVirtualAddress()
            : 0;
#endif
        ctx.passKind = passKind;
        ctx.binding.cmd = ctx.cmd;
        ctx.binding.depthAwarePhase = depthAwarePhase;
        ctx.binding.fallbackTextureHandle = gMeshRendererState.fallbackTextureHandle;
        ctx.binding.fallbackNormalTextureHandle = gMeshRendererState.fallbackNormalTextureHandle;
        ctx.binding.fallbackBlackTextureHandle = gMeshRendererState.fallbackBlackTextureHandle;
        ctx.binding.fallbackCubeTextureHandle = gMeshRendererState.fallbackCubeTextureHandle;
        ctx.binding.passResources = passResources;
        if (ctx.binding.passResources.fallbackAoTextureHandle < 0) {
            ctx.binding.passResources.fallbackAoTextureHandle = gMeshRendererState.fallbackTextureHandle;
        }
        ctx.binding.stats = &gMeshRendererState.debugStats;
        ctx.materialFill.fallbackTextureHandle = gMeshRendererState.fallbackTextureHandle;
        ctx.materialFill.fallbackNormalTextureHandle = gMeshRendererState.fallbackNormalTextureHandle;
        ctx.materialFill.fallbackBlackTextureHandle = gMeshRendererState.fallbackBlackTextureHandle;
        ctx.materialFill.stats = &gMeshRendererState.debugStats;
        ctx.services.device = SERVICES::gCtx.device;
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
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides) {

        if (!EnsureInitialized()) {
            return false;
        }

        gMeshRendererState.frameResources.Activate(SERVICES::gCtx.frameIndex);
        gMeshRendererState.materialResolver.BeginFrame(kMaxMaterialTextureGpuLoadsPerFrame);
        gMeshRendererState.gpuMaterialRegistry.BeginFrame();
        // Capture 逕ｨ縺ｮ蝗ｺ螳夊ｧ｣蜒丞ｺｦ繧・camera constants 縺ｫ蜿肴丐縺吶ｋ縲・
        if (!PrepareMeshFrame(
                camera,
                environment,
                debugView,
                screenWidth,
                screenHeight,
                cameraOverrides)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr ||
            !gMeshRendererState.frameResources.HasActiveDrawResources()) {
            return false;
        }

        gMeshRendererState.frameObjectIndex = 0;
        SyncSurfaceGpuSceneMaterialFrame();
        CommitActiveMaterialDataFrame(cmd);
        if (HasGpuDrivenSceneSource()) {
            UploadMeshShaderJointPalettes();
            gMeshRendererState.traditionalIndirectOwner.RefreshForActivePipeline(
                gMeshRendererState.gpuDrivenSceneSource,
                BuildTraditionalIndirectHydrationContext());
            PrepareGpuDrivenFrameState();
        } else {
            ResetGpuDrivenFrameState();
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
