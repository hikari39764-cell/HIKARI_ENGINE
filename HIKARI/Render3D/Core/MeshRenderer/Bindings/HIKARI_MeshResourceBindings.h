#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"

namespace HIKARI::MESHRENDERER {

    struct MeshRendererDebugStats;

    constexpr UINT kTrackedRootParamCount = ROOT_PARAM::Count;

    struct MeshBindingStateCache {
        ID3D12RootSignature* rootSignature = nullptr;
        ID3D12PipelineState* pipelineState = nullptr;
        std::array<D3D12_GPU_VIRTUAL_ADDRESS, kTrackedRootParamCount> cbvAddresses{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kTrackedRootParamCount> descriptorTables{};
        std::array<uint32_t, kTrackedRootParamCount> rootConstants{};
        std::array<bool, kTrackedRootParamCount> rootConstantValid{};
    };

    struct MaterialTextureHandles {
        int baseColor = -1;
        int normal = -1;
        int emissive = -1;
        int metallicRoughness = -1;
        int occlusion = -1;
        int specular = -1;
        int specularColor = -1;
    };

    struct MeshBindingContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        bool depthAwarePhase = false;
        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        int fallbackCubeTextureHandle = -1;
        MeshPassResources passResources{};
        MeshBindingStateCache* cache = nullptr;
        MeshRendererDebugStats* stats = nullptr;
    };

    void BindFrameCommonResources(
        const MeshBindingContext& ctx,
        ID3D12RootSignature* rootSig,
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress,
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress,
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress,
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress);

    void BindObjectConstantBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS objectAddress);

    void BindObjectDataBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrv);

    void BindObjectDataIndex(
        const MeshBindingContext& ctx,
        uint32_t objectIndex);

    void BindMaterialDataBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv);

    void BindMaterialDataIndex(
        const MeshBindingContext& ctx,
        uint32_t materialIndex);

    void BindSurfaceGpuSceneBuffer(
        const MeshBindingContext& ctx,
        D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv);

    void BindSurfaceGpuSceneControl(
        const MeshBindingContext& ctx,
        uint32_t baseInstanceIndex,
        bool enabled);

    void BindMaterialTexturePool(const MeshBindingContext& ctx);

    void BindClusterGeometryPool(const MeshBindingContext& ctx);

    void BindMeshletVisibleRanges(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS visibleRangeAddress);

    void BindMeshletVisibleClusterList(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS visibleClusterListAddress);

    void BindMeshletDeformationPalettes(
        const MeshBindingContext& ctx,
        D3D12_GPU_VIRTUAL_ADDRESS paletteAddress);

    void BindPipelineState(
        const MeshBindingContext& ctx,
        ID3D12PipelineState* pso);

    void BindShadowMap(const MeshBindingContext& ctx);
    void BindSkyCube(const MeshBindingContext& ctx);
    void BindSceneDepth(const MeshBindingContext& ctx);
    void BindSceneColor(const MeshBindingContext& ctx);
    void BindIblResources(const MeshBindingContext& ctx);
    void BindReflectionProbeResources(const MeshBindingContext& ctx);
    void BindSsao(const MeshBindingContext& ctx);
    void BindLightProbeResources(const MeshBindingContext& ctx);

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyCubeSrv(int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneDepthSrv(
        bool depthAwarePhase,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneColorSrv(
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv,
        int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblIrradianceSrv(int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblPrefilteredSrv(int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblBrdfLutSrv(int fallbackTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveReflectionProbePrefilteredSrv(int fallbackCubeTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSsaoSrv(D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv, int fallbackAoTextureHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveLightProbeShSrv();

} // namespace HIKARI::MESHRENDERER
