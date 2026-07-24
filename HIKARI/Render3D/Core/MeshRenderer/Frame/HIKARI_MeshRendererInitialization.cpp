#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

#include <string>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    bool CreateBuffers(ID3D12Device* device) {
        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }

        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT surfaceGpuSceneSrvIndex =
            GFX::DESCRIPTOR::ToFrameIndex(
                GFX::DESCRIPTOR::SystemSrv::MeshSurfaceGpuSceneFrame0,
                0u);
        const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
            GFX::DESCRIPTOR::CpuAt(
                srvHeap,
                descriptorSize,
                surfaceGpuSceneSrvIndex);
        const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
            GFX::DESCRIPTOR::GpuAt(
                srvHeap,
                descriptorSize,
                surfaceGpuSceneSrvIndex);
        if (!gMeshRendererState.surfaceGpuSceneBuffer.Initialize(
                device,
                surfaceGpuSceneSrvCpu,
                surfaceGpuSceneSrvGpu,
                descriptorSize)) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven mesh pass will be unavailable.");
        }

        if (!gMeshRendererState.frameResources.Initialize(device, srvHeap)) {
            return false;
        }
        gMeshRendererState.frameResources.Activate(SERVICES::gCtx.frameIndex);
        return true;
    }

    bool EnsureInitialized() {
        if (gMeshRendererState.initialized) {
            return true;
        }
        auto* device = SERVICES::gCtx.device;
        if (!device) {
            return false;
        }

        if (!CreateBuffers(device)) {
            return false;
        }
        if (!gMeshRendererState.gpuMaterialRegistry.Initialize(
                kMaxMaterialDataCount,
                GFX::kFrameResourceCount)) {
            DEBUGLOG::PushRenderError(
                "[MeshRenderer][ERROR] GPU material registry initialization failed.");
            return false;
        }
        if (!InitializeMeshPipelines(device, gMeshRendererState.pipelines)) {
            return false;
        }
        if (!gMeshRendererState.traditionalCommandStreamBuffer.Initialize(
            device,
            GetStaticRootSignature(gMeshRendererState.pipelines),
            ROOT_PARAM::SurfaceGpuSceneControl,
            4u)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] GPU Traditional Command Stream draw buffer initialization failed. GPU-compacted surface stream will be unavailable.");
        } else if (!gMeshRendererState.traditionalCommandStreamBuffer.InitializeSkinnedCommandStream(
            device,
            GetSkinnedRootSignature(gMeshRendererState.pipelines),
            ROOT_PARAM::SurfaceGpuSceneControl,
            ROOT_PARAM::JointPalette)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Surface skinned indirect command signature initialization failed. Skinned GPU-driven indirect stream will be unavailable.");
        }
        if (!gMeshRendererState.clusterGpuCullingPass.Initialize(
            device,
            GetStaticRootSignature(gMeshRendererState.pipelines),
            ROOT_PARAM::SurfaceGpuSceneControl,
            4u)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Cluster GPU culling pass initialization failed. Cluster draw seeds will be disabled.");
        }
        if (!gMeshRendererState.meshletRenderBackend.Initialize(
            device,
            GetStaticRootSignature(gMeshRendererState.pipelines),
            RENDER3D::MESHLET::MeshletPipelineMask::MainRenderer)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] Meshlet render backend is not ready. GPU-driven mesh shader route will be unavailable.");
        }
        gMeshRendererState.clusterGpuDrivenProducer.Attach(&gMeshRendererState.clusterGpuCullingPass);
        gMeshRendererState.gpuDrivenLayer.Attach(
            &gMeshRendererState.surfaceGpuSceneBuffer,
            &gMeshRendererState.traditionalCommandStreamBuffer,
            &gMeshRendererState.clusterGpuDrivenProducer);
        if (!gMeshRendererState.gpuDrivenLayer.Initialize(
            device,
            GetStaticRootSignature(gMeshRendererState.pipelines),
            ROOT_PARAM::SurfaceGpuSceneControl,
            4u)) {
            DEBUGLOG::PushRenderError("[MeshRenderer][WARN] GPU-driven layer initialization failed. GPU-driven draws will be unavailable.");
        }
        UpdateMeshletBackendDebugStats();

        // MeshRenderer 蜈ｱ騾・fallback 縺ｯ resource handle 繧呈ｭ｣縺ｨ縺励※菫晄戟縺吶ｋ縲・
        gMeshRendererState.fallbackTextureResource = RENDER3D::LoadTextureResource(
            "mesh_renderer/fallback_white",
            "HIKARI/black1x1.png");
        gMeshRendererState.fallbackTextureHandle =
            RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackTextureResource);
        gMeshRendererState.fallbackNormalTextureResource = RENDER3D::LoadTextureResource(
            "mesh_renderer/fallback_normal",
            "HIKARI/normal_flat_1x1.png");
        gMeshRendererState.fallbackNormalTextureHandle =
            RENDER3D::GetTextureResourceBackendHandle(gMeshRendererState.fallbackNormalTextureResource);
        if (gMeshRendererState.fallbackNormalTextureHandle < 0) {
            gMeshRendererState.fallbackNormalTextureResource = gMeshRendererState.fallbackTextureResource;
            gMeshRendererState.fallbackNormalTextureHandle = gMeshRendererState.fallbackTextureHandle;
        }
        gMeshRendererState.fallbackBlackTextureResource = gMeshRendererState.fallbackTextureResource;
        gMeshRendererState.fallbackBlackTextureHandle = gMeshRendererState.fallbackTextureHandle;
        gMeshRendererState.fallbackCubeTextureResource = RENDER3D::CreateSolidColorCubemapResource(
            "mesh_renderer/fallback_cube",
            0x000000ffu,
            RENDER3D::TextureResourceColorSpace::Linear);
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
        HIKARI_LOG_INFO(
            "[MeshRenderer][Initialize] ready meshlet=" +
            std::string(gMeshRendererState.debugStats.meshletBackendPipelineReady ? "yes" : "no"));
        return true;
    }


} // namespace HIKARI::MESHRENDERER::INTERNAL
