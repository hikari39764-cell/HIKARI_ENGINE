#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <array>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_StartupTimer.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
bool CreateBuffers(const GFX::Context& graphics) {
    ID3D12Device* device = graphics.device;
    ID3D12DescriptorHeap* srvHeap = graphics.srvHeap;
    if (device == nullptr || srvHeap == nullptr) {
        return false;
    }

    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> surfaceGpuSceneSrvCpu{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> surfaceGpuSceneSrvGpu{};
    for (uint32_t frameIndex = 0u; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        const UINT surfaceGpuSceneSrvIndex =
            GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::MeshSurfaceGpuSceneFrameSlot(frameIndex));
        surfaceGpuSceneSrvCpu[frameIndex] = GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
        surfaceGpuSceneSrvGpu[frameIndex] = GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
    }
    if (!gMeshRendererState.surfaceGpuSceneBuffer.Initialize(device, surfaceGpuSceneSrvCpu, surfaceGpuSceneSrvGpu)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven mesh pass will be unavailable.");
    }
    if (!gMeshRendererState.massGpuSceneBuffer.Initialize(device, srvHeap)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] Mass GPU scene admission failed. Compact mass rendering will be unavailable.");
    }
    if (!gMeshRendererState.waterSurfaceGpuFrameBuffer.Initialize(device)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] Water surface feature buffer initialization failed. GPU-driven mesh pass will be unavailable.");
    }
    if (!gMeshRendererState.sandSurfaceGpuFrameBuffer.Initialize(device)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] SandSurface feature buffer initialization failed. Sand displacement will fall back to the base surface.");
    }

    if (!gMeshRendererState.frameResources.Initialize(device, srvHeap)) {
        return false;
    }
    gMeshRendererState.frameResources.Activate(graphics.frameIndex);
    return true;
}

bool EnsureInitialized(const GFX::Context& graphics) {
    if (gMeshRendererState.initialized) {
        return true;
    }
    if (gMeshRendererState.initializationFailed) {
        return false;
    }
    auto* device = graphics.device;
    if (!device || !graphics.srvHeap) {
        return false;
    }

    CORE::StartupTimer initializationTimer("Renderer3D.initialize");

    const auto failInitialization = [](const char* message) {
        DEBUGLOG::PushRenderError(message);
        gMeshRendererState.initializationFailed = true;
        return false;
    };

    CORE::StartupTimer buffersTimer("Renderer3D.frame_resources");
    if (!CreateBuffers(graphics)) {
        return failInitialization("[MeshRenderer][ERROR] Required frame-resource initialization "
                                  "failed. Rendering is disabled until process restart.");
    }
    if (!gMeshRendererState.gpuMaterialRegistry.Initialize(kMaxMaterialDataCount, GFX::kFrameResourceCount)) {
        return failInitialization("[MeshRenderer][ERROR] GPU material registry initialization "
                                  "failed. Rendering is disabled until process restart.");
    }
    buffersTimer.Finish();
    CORE::StartupTimer pipelinesTimer("Renderer3D.traditional_pipelines");
    if (!InitializeMeshPipelines(device, gMeshRendererState.pipelines)) {
        return failInitialization("[MeshRenderer][ERROR] Required mesh pipeline initialization "
                                  "failed. Rendering is disabled until process restart.");
    }
    pipelinesTimer.Finish();
    CORE::StartupTimer cullingTimer("Renderer3D.culling_resources");
    if (!gMeshRendererState.traditionalCommandStreamBuffer.Initialize(
            device, GetStaticRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl, 4u)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] GPU Traditional Command Stream draw buffer initialization failed. GPU-compacted surface stream will be unavailable.");
    } else if (!gMeshRendererState.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
                   device, GetSkinnedRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl,
                   ROOT_PARAM::JointPalette)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] Surface skinned indirect command signature initialization failed. Skinned GPU-driven indirect stream will be unavailable.");
    }
    if (!gMeshRendererState.clusterGpuCullingPass.Initialize(
            graphics, GetStaticRootSignature(gMeshRendererState.pipelines), ROOT_PARAM::SurfaceGpuSceneControl, 4u)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] Cluster GPU culling pass initialization failed. Cluster draw seeds will be disabled.");
    }
    cullingTimer.Finish();
    CORE::StartupTimer meshletTimer("Renderer3D.meshlet_pipelines");
    if (!gMeshRendererState.meshletRenderBackend.Initialize(device,
                                                            GetStaticRootSignature(gMeshRendererState.pipelines),
                                                            RENDER3D::MESHLET::MeshletPipelineMask::MainRenderer)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] Meshlet render backend is not ready. GPU-driven mesh shader route will be unavailable.");
    }
    meshletTimer.Finish();
    // 任意の presentation pass は、最初の非空 PrepareFrame が初期化する。
    gMeshRendererState.clusterGpuDrivenProducer.Attach(&gMeshRendererState.clusterGpuCullingPass);
    gMeshRendererState.gpuDrivenLayer.Attach(
        &gMeshRendererState.surfaceGpuSceneBuffer, &gMeshRendererState.massGpuSceneBuffer,
        &gMeshRendererState.traditionalCommandStreamBuffer, &gMeshRendererState.clusterGpuDrivenProducer,
        &gMeshRendererState.waterSurfaceGpuFrameBuffer, &gMeshRendererState.sandSurfaceGpuFrameBuffer);
    if (!gMeshRendererState.gpuDrivenLayer.Initialize(device, GetStaticRootSignature(gMeshRendererState.pipelines),
                                                      ROOT_PARAM::SurfaceGpuSceneControl, 4u)) {
        DEBUGLOG::PushRenderError(
            "[MeshRenderer][WARN] GPU-driven layer initialization failed. GPU-driven draws will be unavailable.");
    }
    UpdateMeshletBackendDebugStats();

    // MeshRenderer 共通の fallback はメモリ上の単色 texture とし、作業ディレクトリやディスクに依存しない。
    gMeshRendererState.fallbackTextureResource = RENDER3D::CreateSolidColorTextureResource(
        "mesh_renderer/fallback_white", 0xffffffffu, RENDER3D::TextureResourceColorSpace::Srgb);
    gMeshRendererState.fallbackTextureHandle =
        RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackTextureResource);
    gMeshRendererState.fallbackNormalTextureResource = RENDER3D::CreateSolidColorTextureResource(
        "mesh_renderer/fallback_normal", 0x8080ffffu, RENDER3D::TextureResourceColorSpace::Linear);
    gMeshRendererState.fallbackNormalTextureHandle =
        RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackNormalTextureResource);
    if (gMeshRendererState.fallbackNormalTextureHandle < 0) {
        gMeshRendererState.fallbackNormalTextureResource = gMeshRendererState.fallbackTextureResource;
        gMeshRendererState.fallbackNormalTextureHandle = gMeshRendererState.fallbackTextureHandle;
    }
    gMeshRendererState.fallbackBlackTextureResource = RENDER3D::CreateSolidColorTextureResource(
        "mesh_renderer/fallback_black", 0x000000ffu, RENDER3D::TextureResourceColorSpace::Srgb);
    gMeshRendererState.fallbackBlackTextureHandle =
        RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackBlackTextureResource);
    if (gMeshRendererState.fallbackBlackTextureHandle < 0) {
        gMeshRendererState.fallbackBlackTextureResource = gMeshRendererState.fallbackTextureResource;
        gMeshRendererState.fallbackBlackTextureHandle = gMeshRendererState.fallbackTextureHandle;
    }
    gMeshRendererState.fallbackCubeTextureResource = RENDER3D::CreateSolidColorCubemapResource(
        "mesh_renderer/fallback_cube", 0x000000ffu, RENDER3D::TextureResourceColorSpace::Linear);
    gMeshRendererState.fallbackCubeTextureHandle =
        RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackCubeTextureResource);
    if (gMeshRendererState.fallbackCubeTextureHandle < 0) {
        HIKARI_LOG_WARN("[MeshRenderer] fallback cubemap creation failed.");
    }
    MeshMaterialResolverFallbacks fallbacks{};
    fallbacks.whiteTexture = gMeshRendererState.fallbackTextureHandle;
    fallbacks.normalTexture = gMeshRendererState.fallbackNormalTextureHandle;
    fallbacks.blackTexture = gMeshRendererState.fallbackBlackTextureHandle;
    gMeshRendererState.materialResolver.SetFallbacks(fallbacks);

    gMeshRendererState.initialized = true;
    HIKARI_LOG_INFO("[MeshRenderer][Initialize] ready meshlet=" +
                    std::string(gMeshRendererState.debugStats.meshletBackend.pipelineReady ? "yes" : "no"));
    return true;
}

} // namespace HIKARI::MESHRENDERER::INTERNAL
