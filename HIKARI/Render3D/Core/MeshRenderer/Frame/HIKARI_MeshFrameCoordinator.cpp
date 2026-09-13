#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

    namespace {

        constexpr size_t kMaxMaterialTextureGpuLoadsPerFrame = 2;
        constexpr uint64_t kOceanSpectrumRetryDelayFrames = 120u;
        ID3D12Device* gOceanSpectrumInitializationFailedDevice = nullptr;
        uint64_t gOceanSpectrumNextRetryFrame = 0u;
        bool gOceanSpectrumFailureWarningEmitted = false;

    } // namespace

    void ResetOceanSpectrumLazyInitializationState() noexcept {
        gOceanSpectrumInitializationFailedDevice = nullptr;
        gOceanSpectrumNextRetryFrame = 0u;
        gOceanSpectrumFailureWarningEmitted = false;
    }

    void RecordOceanSpectrumForWaterFrame(
        const GFX::Context& graphics,
        const RENDER3D::WATER::WaterSurfaceGpuFrameBuffer& waterSurfaceBuffer) {

        if (!waterSurfaceBuffer.RequiresOceanSpectrum()) {
            return;
        }

        auto& runtime = gMeshRendererState.oceanSpectrumRuntime;
        const uint64_t frameIndex = TIME::GetFrameContext().frameIndex;
        if (!runtime.IsInitialized()) {
            if (gOceanSpectrumInitializationFailedDevice == graphics.device &&
                frameIndex < gOceanSpectrumNextRetryFrame) {
                return;
            }
            if (!runtime.Initialize(graphics)) {
                gOceanSpectrumInitializationFailedDevice = graphics.device;
                gOceanSpectrumNextRetryFrame =
                    frameIndex + kOceanSpectrumRetryDelayFrames;
                if (!gOceanSpectrumFailureWarningEmitted) {
                    gOceanSpectrumFailureWarningEmitted = true;
                    DEBUGLOG::PushRenderError(
                        "[MeshRenderer][WARN] Ocean spectrum lazy initialization failed. Water will use the non-spectral fallback while mesh rendering continues.");
                }
                return;
            }
            ResetOceanSpectrumLazyInitializationState();
        }
        (void)runtime.RecordCompute(
            graphics,
            frameIndex,
            TIME::GetInterpolatedSimulationTimeSeconds());
    }
    MeshFrameResources& GetActiveMeshFrameResources() {
        return gMeshRendererState.frameResources.Active();
    }

    MATH::Mat4 ResolveGpuDrivenCullingViewProj() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.view.IsInitialized()
            ? frame.view.cullingCamera.viewProj
            : MATH::Mat4::Identity();
    }

    MATH::Vec3 ResolveGpuDrivenCullingCameraPosition() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.view.IsInitialized()
            ? MATH::Vec3{
                frame.view.cullingCamera.cameraPos.x,
                frame.view.cullingCamera.cameraPos.y,
                frame.view.cullingCamera.cameraPos.z
            }
            : MATH::Vec3{};
    }

    D3D12_GPU_VIRTUAL_ADDRESS ResolveCameraAddress() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.view.Bindings().camera;
    }

    D3D12_GPU_VIRTUAL_ADDRESS ResolveCullingCameraAddress() {
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        return frame.view.Bindings().cullingCamera;
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetSceneSourcePass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

        return gMeshRendererState.gpuDrivenSceneSource.GetPass(passKind);
    }

    bool HasGpuDrivenPassSource(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind) {

        const bool massPass =
            passKind == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque ||
            passKind == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass ||
            passKind == RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux;
        return GetSceneSourcePass(passKind).HasGpuSceneRange() ||
            (massPass &&
                gMeshRendererState.gpuDrivenSceneSource.HasMassInstances());
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
        if (!frameResources.view.IsInitialized()) return false;

        const MATH::Mat4 viewProj =
            cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                ? *cameraOverrides->renderViewProj : camera.GetViewProj();
        const MATH::Mat4 inverseViewProj =
            cameraOverrides != nullptr && cameraOverrides->HasRenderMatrices()
                ? *cameraOverrides->renderInvViewProj : MATH::Inverse(viewProj);
        const MATH::Vec3 cameraPos = camera.GetPosition();
        const uint32_t screenW = overrideScreenWidth != 0u && overrideScreenHeight != 0u
            ? overrideScreenWidth : static_cast<uint32_t>(std::max(1, graphics.backBufferWidth));
        const uint32_t screenH = overrideScreenWidth != 0u && overrideScreenHeight != 0u
            ? overrideScreenHeight : static_cast<uint32_t>(std::max(1, graphics.backBufferHeight));
        FillCameraCB(viewProj, inverseViewProj, cameraPos, screenW, screenH,
            frameResources.view.camera);
        CameraCB cullingCamera = frameResources.view.camera;
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
            frameResources.view.cullingCamera = gMeshRendererState.frozenCullingCamera;
        } else {
            gMeshRendererState.frozenCullingCameraValid = false;
            gMeshRendererState.frozenCullingCameraWidth = 0;
            gMeshRendererState.frozenCullingCameraHeight = 0;
            frameResources.view.cullingCamera = cullingCamera;
        }
        gMeshRendererState.debugStats.gpuDrivenCullingCameraFrozen =
            gMeshRendererState.freezeGpuDrivenCullingCamera && gMeshRendererState.frozenCullingCameraValid;

        frameResources.view.BuildLocalLights(lighting, camera, static_cast<uint32_t>(screenW), static_cast<uint32_t>(screenH));
        FillLightCB(
            environment,
            frameResources.view.localLightList,
            debugView,
            frameResources.view.light,
            gMeshRendererState.debugStats);
        FillShadowCB(
            environment,
            TIME::GetFrameContext().frameIndex,
            frameResources.view.shadow);
        FillSkyEnvironmentCB(
            environment,
            cameraPos,
            camera.GetTarget(),
            frameResources.view.skyEnvironment);
        return frameResources.view.Publish(graphics, &frameResources.view.cullingCamera);
    }

    MeshDrawContext BuildDrawContext(
        const GFX::Context& graphics,
        bool depthAwarePhase,
        MeshDrawPassKind passKind,
        const MeshPassResources& passResources,
        const MeshFrameBindingOverrides* overrides) {
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
                : frame.view.Bindings().light;
        ctx.localLightsAddress =
            overrides != nullptr &&
                overrides->localLightsAddress != 0
                ? overrides->localLightsAddress
                : frame.view.Bindings().localLights.lights;
        ctx.localLightClusterHeadersAddress =
            overrides != nullptr &&
                overrides->localLightClusterHeadersAddress != 0
                ? overrides->localLightClusterHeadersAddress
                : frame.view.Bindings().localLights.clusterHeaders;
        ctx.localLightClusterIndicesAddress =
            overrides != nullptr &&
                overrides->localLightClusterIndicesAddress != 0
                ? overrides->localLightClusterIndicesAddress
                : frame.view.Bindings().localLights.clusterIndices;
        ctx.shadowAddress =
            overrides != nullptr && overrides->shadowAddress != 0
                ? overrides->shadowAddress
                : frame.view.Bindings().shadow;
        ctx.skyEnvironmentAddress =
            overrides != nullptr &&
                overrides->skyEnvironmentAddress != 0
                ? overrides->skyEnvironmentAddress
                : frame.view.Bindings().skyEnvironment;
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
        const RENDER3D::WATER::SPECTRUM::
            OceanSpectrumOutputDescriptorTable& oceanSpectrumTable =
                gMeshRendererState.oceanSpectrumRuntime.
                    GetOutputDescriptorTable();
        const bool waterRequiresOceanSpectrum =
            waterSurfaceBuffer != nullptr &&
            waterSurfaceBuffer->RequiresOceanSpectrum();
        ctx.oceanSpectrumTable = oceanSpectrumTable.base;
        ctx.oceanSpectrumResolution =
            waterRequiresOceanSpectrum
                ? gMeshRendererState.oceanSpectrumRuntime.GetResolution()
                : 0u;
        ctx.oceanSpectrumValid =
            waterRequiresOceanSpectrum &&
            oceanSpectrumTable.valid &&
            oceanSpectrumTable.descriptorCount ==
                RENDER3D::WATER::SPECTRUM::
                    kOceanSpectrumCascadeCount * 2u &&
            gMeshRendererState.oceanSpectrumRuntime.IsOutputValidForFrame(
                TIME::GetFrameContext().frameIndex);
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
        return gMeshRendererState.gpuDrivenSceneSource.
            HasAnyGpuSceneInstances();
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
        if (!gMeshRendererState.worldSpritePass.PrepareFrame(
                graphics,
                gMeshRendererState.worldSpriteFrameSource,
                camera.GetPosition())) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][WARN] World Sprite frame preparation failed; "
                "mesh rendering continues without World Sprites.");
        }
        if (!gMeshRendererState.worldTextPass.PrepareFrame(
                graphics,
                gMeshRendererState.worldTextFrameSource,
                camera.GetPosition())) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][WARN] World Text frame preparation failed; "
                "mesh rendering continues without World Text.");
        }
        if (!CommitActiveMaterialDataFrame(cmd)) {
            return false;
        }
        if (HasGpuDrivenSceneSource()) {
            UploadMeshShaderJointPalettes();
            gMeshRendererState.traditionalIndirectOwner.RefreshForActivePipeline(
                gMeshRendererState.gpuDrivenSceneSource,
                BuildTraditionalIndirectHydrationContext(graphics));
            PrepareGpuDrivenFrameState(graphics);
        } else {
            ResetGpuDrivenFrameState(graphics);
        }
        RecordOceanSpectrumForWaterFrame(
            graphics,
            gMeshRendererState.waterSurfaceGpuFrameBuffer);

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
