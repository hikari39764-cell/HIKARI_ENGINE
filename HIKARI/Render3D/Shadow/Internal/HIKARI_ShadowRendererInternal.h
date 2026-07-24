#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowCachePolicy.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

namespace HIKARI::SHADOW::INTERNAL {

    using Microsoft::WRL::ComPtr;

    inline constexpr UINT kMaxShadowCasterObjects = 2048u;
    inline constexpr size_t kMaxShadowJointPaletteMatrices = 128u;
    inline constexpr D3D12_RESOURCE_STATES kShadowShaderReadState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    struct ShadowCameraCB {
        MATH::Mat4 lightViewProj{};
        MATH::Mat4 invLightViewProj{};
        MATH::Vec4 lightPosition{};
        MATH::Vec4 timeParams{};
        MATH::Vec4 screenParams{};
    };

    struct ShadowJointPaletteCB {
        MATH::Mat4 jointMatrices[kMaxShadowJointPaletteMatrices]{};
    };

    struct ShadowFrameResources {
        ComPtr<ID3D12Resource> cameraCB;
        ComPtr<ID3D12Resource> materialDataUploadBuffer;
        ComPtr<ID3D12Resource> materialDataBuffer;
        ComPtr<ID3D12Resource> jointPaletteCB;
        ShadowCameraCB* cameraMapped = nullptr;
        MESHRENDERER::MaterialGpuData* materialDataMapped = nullptr;
        ShadowJointPaletteCB* jointPaletteMapped = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
        D3D12_RESOURCE_STATES materialDataState = D3D12_RESOURCE_STATE_COMMON;
    };

    struct ShadowMaterialFrameTable {
        std::unordered_map<uint64_t, uint32_t> indexByKey{};
        std::unordered_set<uint32_t> textureDescriptorIndices{};
        uint32_t count = 0;

        void Clear() {
            indexByKey.clear();
            textureDescriptorIndices.clear();
            count = 0;
        }
    };

    struct ShadowRendererState {
        bool initialized = false;
        bool frameEnabled = false;
        bool frameHasShadowWork = false;
        bool frameHasStaticShadowWork = false;
        bool frameHasDynamicShadowWork = false;
        uint32_t resolution = 0;
        D3D12_RESOURCE_STATES shadowState = kShadowShaderReadState;
        D3D12_RESOURCE_STATES staticShadowState = D3D12_RESOURCE_STATE_COMMON;
        MATH::Mat4 lightViewProj = MATH::Mat4::Identity();
        MATH::Vec3 lightCullPosition{};
        MATH::Vec3 lightAnchor{};
        float lightAnchorGrid = 0.0f;
        float elapsedTimeSec = 0.0f;

        ComPtr<ID3D12Resource> shadowMap;
        ComPtr<ID3D12Resource> staticShadowMap;
        ComPtr<ID3D12DescriptorHeap> dsvHeap;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        RENDER3D::TextureResourceHandle shadowSrvResource{};
        RENDER3D::TextureResourceHandle fallbackTextureResource{};
        int shadowSrvHandle = -1;
        int fallbackTextureHandle = -1;

        ComPtr<ID3D12RootSignature> rootSig;
        ComPtr<ID3D12RootSignature> skinnedRootSig;
        ComPtr<ID3D12PipelineState> staticPso;
        ComPtr<ID3D12PipelineState> skinnedPso;
        std::array<ShadowFrameResources, GFX::kFrameResourceCount> frameResources{};
        uint32_t activeFrameResourceIndex = 0;
        ShadowMaterialFrameTable materialDataFrameTable{};

        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSource = nullptr;
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource shadowSceneSource{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource staticShadowSceneSource{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource dynamicShadowSceneSource{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource activeShadowSceneSource{};
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>
            staticShadowPrimaryInstances{};
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>
            staticShadowPrimaryMaterialSources{};
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>
            dynamicShadowPrimaryInstances{};
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>
            dynamicShadowPrimaryMaterialSources{};
        ShadowSourceCacheState shadowSourceCache{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer
            traditionalCommandStreamBuffer{};
        RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
        RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
        RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
        RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter
            clusterGpuDrivenProducer{};
        RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};
        std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>>
            primitiveMeshCache{};
        std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>>
            primitiveSkinnedMeshCache{};
        std::unordered_map<std::string, RENDER3D::TextureResourceHandle>
            materialTextureCache{};
        ShadowMapDebugStats debugStats{};
        size_t shadowMapRecreateCount = 0;
        ShadowCachePolicy shadowCache{};
    };

    extern ShadowRendererState gShadowRendererState;

    inline ShadowFrameResources& GetActiveShadowFrameResources() {
        return gShadowRendererState.frameResources[
            gShadowRendererState.activeFrameResourceIndex %
            GFX::kFrameResourceCount];
    }

    void InvalidateShadowSourceCache();
    bool CanReuseShadowSourceCache();
    void PublishShadowCacheStats();
    void InvalidateShadowCache();
    bool CanReuseShadowCache(uint32_t resolution);
    void AccumulateShadowMeshletStatsDelta(
        const RENDER3D::MESHLET::MeshletRenderBackendStats& before,
        const RENDER3D::MESHLET::MeshletRenderBackendStats& after);
    void MarkShadowCacheHit();
    void MarkShadowCacheMiss();
    void MarkShadowCacheValidAfterRender();

    void ClearShadowTraditionalIndirectStreams();
    void UploadShadowMeshShaderJointPalettes();
    bool BuildShadowGpuDrivenSceneSources();
    void SyncShadowGpuDrivenBackendAvailability();

    void BindShadowGpuDrivenFrameResources(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* meshletVisibleRangeBuffer,
        ID3D12Resource* meshletVisibleClusterListBuffer = nullptr,
        bool skinnedRoot = false);
    void ResetShadowMaterialFrame();
    void PrepareShadowSurfaceGpuSceneMaterialFrame();
    uint64_t AppendShadowHashValue(uint64_t seed, uint64_t value);
    uint64_t AppendShadowHashBytes(
        uint64_t seed,
        const void* data,
        size_t size);
    uint64_t AppendShadowHashString(
        uint64_t seed,
        const std::string& value);
    bool IsPrimaryShadowCaster(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance);
    bool IsStaticPrimaryShadowCaster(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance);
    void AppendShadowPrimaryInstance(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>& instances,
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>&
            materialSources);
    uint64_t BuildShadowSourceLayoutHash(
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass);
    uint64_t BuildShadowSourceContentHash(
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass);

    D3D12_GPU_VIRTUAL_ADDRESS ResolveShadowJointPaletteAddress(
        size_t objectIndex);
    size_t UploadShadowJointPalette(
        size_t objectIndex,
        const std::vector<MATH::Mat4>& palette);
    void CommitShadowMaterialDataFrame(
        ID3D12GraphicsCommandList* commandList);
    void ActivateShadowFrameResources(uint32_t frameIndex);
    bool CreateShadowFrameResources(ID3D12Device* device);

    bool CreateShadowMapResources(uint32_t resolution);
    void RestoreMainRenderTarget();
    void PrepareFinalShadowMapForDepthWrite(
        ID3D12GraphicsCommandList* commandList,
        bool clearDepth);
    void FinishFinalShadowMap(ID3D12GraphicsCommandList* commandList);
    bool CopyStaticShadowCacheToFinal(
        ID3D12GraphicsCommandList* commandList);
    bool UpdateStaticShadowCacheFromFinal(
        ID3D12GraphicsCommandList* commandList);

    bool CreateShadowPipelineState(ID3D12Device* device);
    bool EnsureShadowRendererInitialized();

    void UploadShadowCameraConstants(const ShadowLightFrame& frame);
    void SubmitShadowDebugFrustum(
        const SceneEnvironment& environment,
        const Camera3D& camera);
    void ClearShadowFrameSubmissions();
    bool PrepareShadowSourceForDraw(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);
    void ResetShadowGpuDrivenWorkFrame();
    void BuildShadowGpuDrivenWorkFrame();
    void UploadShadowIndirectDrawFrame();
    bool ExecuteShadowGpuDrivenPass(
        GFX::GPU_PROFILE::Pass traditionalProfilePass,
        GFX::GPU_PROFILE::Pass meshletProfilePass);

} // namespace HIKARI::SHADOW::INTERNAL
