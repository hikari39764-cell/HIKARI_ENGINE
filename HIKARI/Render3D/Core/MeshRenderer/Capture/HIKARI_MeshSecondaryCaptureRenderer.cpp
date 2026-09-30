#include "Render3D/Core/HIKARI_MeshRenderer.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {

using namespace INTERNAL;

namespace {
constexpr size_t kCaptureMaterialTextureLoadsPerFrame = 4u;
constexpr size_t kCaptureClusterPageTaskCapacity = 131072u;
constexpr size_t kCaptureVisibleRangeCapacity = 131072u;
constexpr size_t kCaptureCandidateCommandCapacity = 131072u;

enum class CaptureFailureStage : uint8_t {
    None,
    InvalidRequest,
    RendererUnavailable,
    ViewResourcesUnavailable,
    FrameResourcesUnavailable,
    ParticlePreparationFailed,
    MaterialUploadFailed,
    SceneResidencyUnavailable,
    OpaqueBackendUnavailable,
    TransparentBackendUnavailable,
};

CaptureFailureStage gLastCaptureFailureStage = CaptureFailureStage::None;

std::string_view CaptureFailureStageName(CaptureFailureStage stage) noexcept {
    switch (stage) {
    case CaptureFailureStage::InvalidRequest: return "InvalidRequest";
    case CaptureFailureStage::RendererUnavailable: return "RendererUnavailable";
    case CaptureFailureStage::ViewResourcesUnavailable: return "ViewResourcesUnavailable";
    case CaptureFailureStage::FrameResourcesUnavailable: return "FrameResourcesUnavailable";
    case CaptureFailureStage::ParticlePreparationFailed: return "ParticlePreparationFailed";
    case CaptureFailureStage::MaterialUploadFailed: return "MaterialUploadFailed";
    case CaptureFailureStage::SceneResidencyUnavailable: return "SceneResidencyUnavailable";
    case CaptureFailureStage::OpaqueBackendUnavailable: return "OpaqueBackendUnavailable";
    case CaptureFailureStage::TransparentBackendUnavailable: return "TransparentBackendUnavailable";
    case CaptureFailureStage::None:
    default: return "None";
    }
}

void ReportCaptureFailure(CaptureFailureStage stage) {
    if (stage == CaptureFailureStage::None || gLastCaptureFailureStage == stage) {
        return;
    }
    gLastCaptureFailureStage = stage;
    HIKARI_LOG_WARN("[SecondarySceneCapture] stage=" + std::string(CaptureFailureStageName(stage)) +
                    "; the valid clear/sky frame remains publishable.");
}

void ReportCaptureReady() {
    if (gLastCaptureFailureStage == CaptureFailureStage::None) {
        return;
    }
    HIKARI_LOG_INFO("[SecondarySceneCapture] recovered from stage=" +
                    std::string(CaptureFailureStageName(gLastCaptureFailureStage)) + ".");
    gLastCaptureFailureStage = CaptureFailureStage::None;
}

void ResetCaptureResources(SecondaryMeshViewResources& resources) noexcept {
    resources.particlePass.Shutdown();
    resources.worldSpritePass.Shutdown();
    resources.worldTextPass.Shutdown();
    for (SecondaryMeshViewFrameResources& frame : resources.frames) {
        frame = {};
    }
    resources.gpuDrivenLayer = {};
    resources.clusterGpuDrivenProducer.Attach(nullptr);
    resources.clusterGpuCullingPass.Reset();
    resources.traditionalCommandStreamBuffer = {};
    resources.surfaceGpuSceneBuffer = {};
    resources.waterSurfaceGpuFrameBuffer = {};
    resources.sceneResidency.Reset();
    for (RENDER3D::RenderResourceView& view : resources.surfaceGpuSceneViews) {
        if (view.IsValid()) {
            (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
        }
        view = {};
    }
    for (RENDER3D::RenderResourceView& view : resources.massGpuSceneNullViews) {
        if (view.IsValid()) {
            (void)RENDER3D::ReleaseRenderResourceDescriptor(view);
        }
        view = {};
    }
    resources.clusterGpuDrivenReady = false;
    resources.traditionalCommandStreamReady = false;
    resources.ownerDevice = nullptr;
    resources.ownerDeviceGeneration = 0u;
    resources.initialized = false;
}

bool EnsureCaptureResources(SecondaryMeshViewResources& resources, const GFX::Context& graphics) {
    ID3D12Device* device = graphics.device;
    if (device == nullptr || GetStaticRootSignature(gMeshRendererState.pipelines) == nullptr) {
        return false;
    }
    const bool ownerChanged = resources.initialized && (resources.ownerDevice != device ||
                                                        resources.ownerDeviceGeneration != graphics.deviceGeneration);
    if (ownerChanged) {
        ResetCaptureResources(resources);
    }
    if (resources.initialized) {
        return true;
    }
    for (SecondaryMeshViewFrameResources& frame : resources.frames) {
        if (!frame.view.Initialize(device)) {
            ResetCaptureResources(resources);
            return false;
        }
    }

    RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphics);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneSrvCpu{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> sceneSrvGpu{};
    for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        const std::vector<RENDER3D::RenderResourceView> range = RENDER3D::AllocateRenderResourceDescriptorRange(2u);
        if (range.size() != 2u) {
            ResetCaptureResources(resources);
            return false;
        }
        RENDER3D::RenderResourceView& view = resources.surfaceGpuSceneViews[frameIndex];
        view = range[0];
        RENDER3D::RenderResourceView& massNullView = resources.massGpuSceneNullViews[frameIndex];
        massNullView = range[1];
        if (!view.IsValid() || !massNullView.IsValid()) {
            ResetCaptureResources(resources);
            return false;
        }
        sceneSrvCpu[frameIndex] = view.cpu;
        sceneSrvGpu[frameIndex] = view.gpu;
        D3D12_SHADER_RESOURCE_VIEW_DESC nullMassSrv{};
        nullMassSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullMassSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullMassSrv.Format = DXGI_FORMAT_UNKNOWN;
        nullMassSrv.Buffer.NumElements = 1u;
        nullMassSrv.Buffer.StructureByteStride = sizeof(RENDER3D::RUNTIME::MassRenderProxyGpuRecord);
        device->CreateShaderResourceView(nullptr, &nullMassSrv, massNullView.cpu);
    }
    if (!resources.surfaceGpuSceneBuffer.Initialize(device, sceneSrvCpu, sceneSrvGpu) ||
        !resources.waterSurfaceGpuFrameBuffer.Initialize(device)) {
        ResetCaptureResources(resources);
        return false;
    }
    resources.clusterGpuDrivenReady = resources.clusterGpuCullingPass.Initialize(
        graphics, GetStaticRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl, 4u,
        kCaptureClusterPageTaskCapacity, kCaptureVisibleRangeCapacity, kCaptureCandidateCommandCapacity);
    resources.traditionalCommandStreamReady =
        resources.traditionalCommandStreamBuffer.Initialize(
            device, GetStaticRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl, 4u) &&
        resources.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
            device, GetSkinnedRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl,
            ROOT_PARAM::JointPalette);
    resources.clusterGpuDrivenProducer.Attach(resources.clusterGpuDrivenReady ? &resources.clusterGpuCullingPass
                                                                              : nullptr);
    resources.gpuDrivenLayer.Attach(
        &resources.surfaceGpuSceneBuffer, nullptr,
        resources.traditionalCommandStreamReady ? &resources.traditionalCommandStreamBuffer : nullptr,
        resources.clusterGpuDrivenReady ? &resources.clusterGpuDrivenProducer : nullptr,
        &resources.waterSurfaceGpuFrameBuffer, &gMeshRendererState.terrainSurfaceGpuFrameBuffer);
    if (!resources.gpuDrivenLayer.Initialize(device, GetStaticRootSignature(gMeshRendererState.pipelines),
                                             ROOT_PARAM::SurfaceGpuSceneControl, 4u)) {
        ResetCaptureResources(resources);
        return false;
    }
    resources.ownerDevice = device;
    resources.ownerDeviceGeneration = graphics.deviceGeneration;
    resources.initialized = true;
    return true;
}

void FillCaptureFrame(SecondaryMeshViewFrameResources& frame, const Camera3D& camera,
                      const SceneEnvironment& environment, const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                      uint32_t width, uint32_t height) {
    CameraCB& cameraData = frame.view.camera;
    const MATH::Vec3 position = camera.GetPosition();
    const MATH::Mat4 viewProj = camera.GetViewProj();
    FillCameraCB(viewProj, MATH::Inverse(viewProj), position, width, height, cameraData);
    frame.view.BuildLocalLights(lighting, camera, width, height);
    FillLightCB(environment, frame.view.localLightList, RenderDebugView::None, frame.view.light,
                gMeshRendererState.debugStats);
    FillShadowCB(environment, TIME::GetFrameContext().frameIndex, frame.view.shadow);
    FillSkyEnvironmentCB(environment, position, camera.GetTarget(), frame.view.skyEnvironment);
}
} // namespace

bool RenderSecondarySceneCapture(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                 const SceneEnvironment& environment,
                                 const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t captureSlot,
                                 uint32_t screenWidth, uint32_t screenHeight, bool renderTransparent,
                                 const MeshPassResources* captureResources) {
    if (view.cameraFrame == nullptr || !view.cameraFrame->valid || captureSlot >= kSecondarySceneCaptureMaximumCount ||
        screenWidth == 0u || screenHeight == 0u || graphics.cmdList == nullptr) {
        ReportCaptureFailure(CaptureFailureStage::InvalidRequest);
        return false;
    }
    if (!EnsureInitialized(graphics)) {
        ReportCaptureFailure(CaptureFailureStage::RendererUnavailable);
        return false;
    }
    SecondaryMeshViewResources& resources = gMeshRendererState.secondaryCaptures[captureSlot];
    if (!EnsureCaptureResources(resources, graphics)) {
        ReportCaptureFailure(CaptureFailureStage::ViewResourcesUnavailable);
        return false;
    }
    gMeshRendererState.frameResources.Activate(graphics.frameIndex);
    if (!gMeshRendererState.frameResources.HasActiveDrawResources()) {
        ReportCaptureFailure(CaptureFailureStage::FrameResourcesUnavailable);
        return false;
    }
    gMeshRendererState.materialResolver.BeginFrame(kCaptureMaterialTextureLoadsPerFrame);
    gMeshRendererState.gpuMaterialRegistry.BeginFrame();

    SecondaryMeshViewFrameResources& captureFrame = resources.frames[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!captureFrame.IsReady()) {
        ReportCaptureFailure(CaptureFailureStage::FrameResourcesUnavailable);
        return false;
    }
    const Camera3D& camera = view.cameraFrame->camera;
    FillCaptureFrame(captureFrame, camera, environment, lighting, screenWidth, screenHeight);
    if (!captureFrame.view.Publish(graphics)) {
        ReportCaptureFailure(CaptureFailureStage::FrameResourcesUnavailable);
        return false;
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource = gMeshRendererState.gpuDrivenSceneSource;
    captureFrame.jointPaletteAddress =
        RENDER3D::GPUDRIVEN::UploadGpuSceneJointPaletteSnapshot(graphics, sceneSource.meshShaderJointPalettes);
    if (captureFrame.jointPaletteAddress == 0u) {
        ReportCaptureFailure(CaptureFailureStage::FrameResourcesUnavailable);
        return false;
    }
    const uintptr_t sourceIdentity = reinterpret_cast<uintptr_t>(&sceneSource);
    SyncGpuDrivenSceneMaterialSources(graphics, sourceIdentity, sceneSource);
    if (!resources.particlePass.PrepareFrame(graphics, renderTransparent ? view.particleFrameSource : nullptr,
                                             gMeshRendererState.gpuMaterialRegistry)) {
        ReportCaptureFailure(CaptureFailureStage::ParticlePreparationFailed);
        return false;
    }
    if (!resources.worldSpritePass.PrepareFrame(graphics, view.worldSpriteFrameSource, camera)) {
        HIKARI_LOG_WARN("[SecondarySceneCapture] World Sprite preparation failed; "
                        "capture remains available without World Sprites.");
    }
    if (!resources.worldTextPass.PrepareFrame(graphics, renderTransparent ? view.worldTextFrameSource : nullptr,
                                              camera.GetPosition())) {
        HIKARI_LOG_WARN("[SecondarySceneCapture] World Text preparation failed; "
                        "capture remains available without World Text.");
    }
    if (!CommitActiveMaterialDataFrame(graphics.cmdList)) {
        ReportCaptureFailure(CaptureFailureStage::MaterialUploadFailed);
        return false;
    }

    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer = resources.gpuDrivenLayer;
    layer.BeginFrame(&sceneSource);
    RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc upload{};
    upload.commandList = graphics.cmdList;
    upload.residency = &resources.sceneResidency;
    upload.frameIndex = graphics.frameIndex;
    const std::span<const uint32_t> materialBindings =
        gMeshRendererState.gpuMaterialRegistry.GetSourceBindings(sourceIdentity);
    upload.materialSlotBySourceRecord = materialBindings.data();
    upload.materialSourceRecordCount = materialBindings.size();
    upload.materialBindingVersion = gMeshRendererState.gpuMaterialRegistry.GetBindingVersion(sourceIdentity);
    const auto& uploadStats = layer.UploadSceneFrame(upload);
    if (sceneSource.CountGpuSceneInstances() != 0u && !uploadStats.sceneResident) {
        ReportCaptureFailure(CaptureFailureStage::SceneResidencyUnavailable);
        return false;
    }
    RecordOceanSpectrumForWaterFrame(graphics, resources.waterSurfaceGpuFrameBuffer);

    RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
    const auto& meshletStats = gMeshRendererState.meshletRenderBackend.GetStats();
    availability.meshShaderForwardPipelineReady = meshletStats.forwardPipelineReady &&
                                                  meshletStats.depthAwarePipelineReady &&
                                                  meshletStats.transparentPipelineReady;
    availability.meshShaderGeometryAuxPipelineReady = meshletStats.geometryAuxPipelineReady;
    availability.traditionalIndirectPipelineReady =
        resources.traditionalCommandStreamReady && AreTraditionalMeshPipelineInputsReady(gMeshRendererState.pipelines);
    layer.SetBackendAvailability(availability);

    const MATH::Mat4 cullViewProj = captureFrame.view.camera.viewProj;
    RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame =
        RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(sceneSource));
    resources.clusterGpuDrivenProducer.BeginFrame(false);
    if (resources.clusterGpuDrivenReady) {
        RENDER3D::GPUDRIVEN::GpuDrivenWorkContext work{};
        work.producer = &resources.clusterGpuDrivenProducer;
        work.commandList = graphics.cmdList;
        work.viewProj = cullViewProj;
        work.cameraPosition = camera.GetPosition();
        work.geometryPoolSrv = RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
        work.surfaceGpuSceneGpuAddress = resources.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
        work.frame = &gpuDrivenFrame;
        work.passMask =
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque) |
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
        work.collectCounterReadback = false;
        (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(work);
    }
    layer.ImportProducerOutput(resources.clusterGpuDrivenProducer.BuildFrameOutput());

    RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrame{};
    commandFrame.commandList = graphics.cmdList;
    commandFrame.cullViewProj = &cullViewProj;
    commandFrame.frameIndex = graphics.frameIndex;
    commandFrame.enableSurfaceFrustumCull = true;
    layer.BuildCommandFrame(commandFrame);

    MeshFrameBindingOverrides overrides{};
    overrides.cameraAddress = captureFrame.view.Bindings().camera;
    overrides.cullingCameraAddress = overrides.cameraAddress;
    overrides.lightAddress = captureFrame.view.Bindings().light;
    overrides.localLightsAddress = captureFrame.view.Bindings().localLights.lights;
    overrides.localLightClusterHeadersAddress = captureFrame.view.Bindings().localLights.clusterHeaders;
    overrides.localLightClusterIndicesAddress = captureFrame.view.Bindings().localLights.clusterIndices;
    overrides.shadowAddress = captureFrame.view.Bindings().shadow;
    overrides.skyEnvironmentAddress = captureFrame.view.Bindings().skyEnvironment;
    overrides.jointPaletteAddress = captureFrame.jointPaletteAddress;
    overrides.surfaceGpuSceneFrameBuffer = &resources.surfaceGpuSceneBuffer;
    overrides.waterSurfaceGpuFrameBuffer = &resources.waterSurfaceGpuFrameBuffer;
    overrides.traditionalCommandStreamBuffer =
        resources.traditionalCommandStreamReady ? &resources.traditionalCommandStreamBuffer : nullptr;

    const MeshPassResources passResources = captureResources ? *captureResources : MeshPassResources{};
    const GeometryBackendExecutionResult opaque = ExecuteSecondaryViewGeometryBackendPlan(
        graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque, passResources, MeshDrawPassKind::Forward,
        RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque, layer, overrides, true);
    const bool worldSpriteOpaqueOk = resources.worldSpritePass.ExecuteForwardOpaque(graphics, overrides.cameraAddress);
    bool transparentOk = true;
    if (renderTransparent) {
        if (captureResources && !passResources.compositionTarget.BeginDepthRead())
            return false;
        const auto& transparentState =
            layer.GetPassExecutionState(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
        if (transparentState.hasSource) {
            const GeometryBackendExecutionResult transparent = ExecuteSecondaryViewGeometryBackendPlan(
                graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent, passResources,
                MeshDrawPassKind::Forward, RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent, layer, overrides,
                true);
            transparentOk = transparent.gpuBackendExecuted;
        }
        transparentOk =
            transparentOk && resources.worldSpritePass.ExecuteForwardTransparent(graphics, overrides.cameraAddress);
        transparentOk = transparentOk &&
                        ExecuteParticleForwardTransparent(resources.particlePass, graphics, overrides.cameraAddress,
                                                          camera.GetPosition(), passResources);
        transparentOk =
            transparentOk && resources.worldTextPass.ExecuteForwardTransparent(graphics, overrides.cameraAddress);
    }
    if (renderTransparent && captureResources)
        passResources.compositionTarget.EndDepthRead();
    const bool emptyScene = !sceneSource.HasAnyGpuSceneRanges();
    if ((!opaque.gpuBackendExecuted && !emptyScene) || !worldSpriteOpaqueOk) {
        ReportCaptureFailure(CaptureFailureStage::OpaqueBackendUnavailable);
        return false;
    }
    if (!transparentOk) {
        ReportCaptureFailure(CaptureFailureStage::TransparentBackendUnavailable);
        return false;
    }
    ReportCaptureReady();
    return true;
}

void ResetSecondarySceneCaptureResources(uint32_t captureSlot) noexcept {
    try {
        if (captureSlot < kSecondarySceneCaptureMaximumCount) {
            ResetCaptureResources(gMeshRendererState.secondaryCaptures[captureSlot]);
        }
        gLastCaptureFailureStage = CaptureFailureStage::None;
    } catch (...) {
    }
}

void ResetSecondarySceneCaptureResources() noexcept {
    for (uint32_t captureSlot = 0u; captureSlot < kSecondarySceneCaptureMaximumCount; ++captureSlot) {
        ResetSecondarySceneCaptureResources(captureSlot);
    }
}

} // namespace HIKARI::MESHRENDERER
