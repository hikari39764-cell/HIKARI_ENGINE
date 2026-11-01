#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
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
#include "Render3D/GpuDriven/CommandStream/HIKARI_OwnedTraditionalIndirectStream.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuFrameBuffer.h"
#include "Render3D/Shadow/HIKARI_ShadowCachePolicy.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowDepthPipelines.h"

namespace HIKARI::SHADOW::INTERNAL {

using Microsoft::WRL::ComPtr;

inline constexpr D3D12_RESOURCE_STATES kShadowShaderReadState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

struct ShadowCameraCB {
    MATH::Mat4 lightViewProj{};
    MATH::Mat4 invLightViewProj{};
    MATH::Vec4 lightPosition{};
    MATH::Vec4 timeParams{};
    MATH::Vec4 simulationTimeParams{};
    MATH::Vec4 screenParams{};
};

struct ShadowFrameResources {
    std::array<D3D12_GPU_VIRTUAL_ADDRESS, kMaxDirectionalShadowCascades> cameraAddresses{};
    DX::DynamicUploadBuffer frameUploads;
    std::vector<MESHRENDERER::MaterialGpuData> materialDataStaging;
    D3D12_GPU_VIRTUAL_ADDRESS materialProgramParametersAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialProgramResourcesAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialCameraAddress = 0;
    ComPtr<ID3D12Resource> materialDataBuffer;
    D3D12_GPU_VIRTUAL_ADDRESS meshJointPaletteAddress = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
    D3D12_RESOURCE_STATES materialDataState = D3D12_RESOURCE_STATE_COMMON;
};

struct ShadowMaterialFrameTable {
    std::unordered_map<uint64_t, uint32_t> indexByKey{};
    std::vector<MATH::Vec4> programParameters;
    std::vector<uint32_t> programResources;
    uint32_t count = 0;
    bool complete = true;

    void Clear() {
        indexByKey.clear();
        programParameters.clear();
        programResources.clear();
        count = 0;
        complete = true;
    }
};

struct ShadowRendererState {
    MESHRENDERER::CameraCB materialCamera{};
    RENDER3D::WORLD_SPRITES::WorldSpritePass* worldSprites = nullptr;
    bool initialized = false;
    bool frameEnabled = false;
    bool debugFrustumVisible = false;
    bool frameHasShadowWork = false;
    bool frameHasStaticShadowWork = false;
    bool frameHasDynamicShadowWork = false;
    uint32_t resolution = 0;
    uint32_t atlasResolution = 0;
    uint32_t atlasTilesPerRow = 2;
    uint32_t activeCascadeIndex = 0;
    uint32_t cascadeCount = 1;
    D3D12_RESOURCE_STATES shadowState = kShadowShaderReadState;
    D3D12_RESOURCE_STATES staticShadowState = D3D12_RESOURCE_STATE_COMMON;
    MATH::Mat4 lightViewProj = MATH::Mat4::Identity();
    std::array<MATH::Mat4, kMaxDirectionalShadowCascades> cascadeLightViewProj{};
    MATH::Vec4 cascadeSplitDistances{};
    std::array<ShadowLightFrame, kMaxDirectionalShadowCascades> cascadeFrames{};
    MATH::Vec3 lightCullPosition{};
    MATH::Vec3 lightAnchor{};
    float lightAnchorGrid = 0.0f;

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
    PIPELINE::ShadowDepthPipelines depthPipelines{};
    std::array<ShadowFrameResources, GFX::kFrameResourceCount> frameResources{};
    std::vector<RENDER3D::RenderResourceView> frameDescriptors{};
    uint32_t activeFrameResourceIndex = 0;
    ShadowMaterialFrameTable materialDataFrameTable{};

    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSource = nullptr;
    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource shadowSceneSource{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource staticShadowSceneSource{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource dynamicShadowSceneSource{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource activeShadowSceneSource{};
    std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> staticShadowPrimaryInstances{};
    std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> staticShadowPrimaryMaterialSources{};
    std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance> dynamicShadowPrimaryInstances{};
    std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource> dynamicShadowPrimaryMaterialSources{};
    RENDER3D::GPUDRIVEN::OwnedTraditionalIndirectStream staticTraditionalIndirectStream{};
    RENDER3D::GPUDRIVEN::OwnedTraditionalIndirectStream dynamicTraditionalIndirectStream{};
    struct ShadowPrimaryLocation {
        uint32_t localIndex = RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
        bool staticCaster = false;
    };
    std::vector<ShadowPrimaryLocation> shadowPrimaryLocations{};
    ShadowSourceCacheState shadowSourceCache{};
    RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
    RENDER3D::TERRAIN::TerrainSurfaceGpuFrameBuffer terrainSurfaceGpuFrameBuffer{};
    RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
    RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
    RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
    RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
    RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
    RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};
    std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache{};
    std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache{};
    std::unordered_map<std::string, RENDER3D::TextureResourceHandle> materialTextureCache{};
    ShadowMapDebugStats debugStats{};
    size_t shadowMapRecreateCount = 0;
    ShadowCachePolicy shadowCache{};
};

extern ShadowRendererState gShadowRendererState;

inline ShadowFrameResources& GetActiveShadowFrameResources(ShadowRendererState& state) {
    return state.frameResources[state.activeFrameResourceIndex % GFX::kFrameResourceCount];
}

inline D3D12_GPU_VIRTUAL_ADDRESS GetActiveShadowCameraGpuAddress(ShadowRendererState& state) {
    ShadowFrameResources& frame = GetActiveShadowFrameResources(state);
    return state.activeCascadeIndex < frame.cameraAddresses.size() ? frame.cameraAddresses[state.activeCascadeIndex]
                                                                   : 0u;
}

void InvalidateShadowSourceCache(ShadowRendererState& state);
bool CanReuseShadowSourceCache(ShadowRendererState& state);
void PublishShadowCacheStats(ShadowRendererState& state);
void InvalidateShadowCache(ShadowRendererState& state);
bool CanReuseShadowCache(ShadowRendererState& state, uint32_t resolution);
void AccumulateShadowMeshletStatsDelta(ShadowRendererState& state,
                                       const RENDER3D::MESHLET::MeshletRenderBackendStats& before,
                                       const RENDER3D::MESHLET::MeshletRenderBackendStats& after);
void MarkShadowCacheHit(ShadowRendererState& state);
void MarkShadowCacheMiss(ShadowRendererState& state);
void MarkShadowCacheValidAfterRender(ShadowRendererState& state);

void ClearShadowTraditionalIndirectStreams(ShadowRendererState& state);
bool PrepareShadowDeformation(ShadowRendererState& state, const GFX::Context& graphics);
bool BuildShadowGpuDrivenSceneSources(ShadowRendererState& state);
void SyncShadowGpuDrivenBackendAvailability(ShadowRendererState& state);

void BindShadowGpuDrivenFrameResources(ShadowRendererState& state, const GFX::Context& graphics,
                                       ID3D12GraphicsCommandList* commandList,
                                       ID3D12Resource* meshletVisibleRangeBuffer, bool skinnedRoot = false);
void ResetShadowMaterialFrame(ShadowRendererState& state);
void PrepareShadowSurfaceGpuSceneMaterialFrame(ShadowRendererState& state);
uint64_t AppendShadowHashValue(uint64_t seed, uint64_t value);
uint64_t AppendShadowHashBytes(uint64_t seed, const void* data, size_t size);
uint64_t AppendShadowHashString(uint64_t seed, const std::string& value);
bool IsPrimaryShadowCaster(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance);
bool MayAffectLocalShadow(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance, const MATH::Vec3& lightPosition,
                          float lightRange) noexcept;
bool IsStaticPrimaryShadowCaster(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
                                 const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* material = nullptr);
bool UpdateShadowPrimaryInstance(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
                                 const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
                                 uint32_t localIndex, RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
                                 RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& material);
// 影の元を一つ書き換え、前後の内容の要約と、並び（形・材質の資源）が変わったかを返す。
struct ShadowPrimaryPatch {
    uint64_t before = 0;
    uint64_t after = 0;
    bool layoutChanged = false;
};
ShadowPrimaryPatch PatchShadowPrimaryInstance(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
                                              const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
                                              uint32_t localIndex, RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
                                              RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& material);
void AppendShadowPrimaryInstance(const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
                                 const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
                                 std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>& instances,
                                 std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources);
uint64_t BuildShadowSourceLayoutHash(const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass);
uint64_t BuildShadowSourceContentHash(const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass);

bool UploadShadowMaterialCamera(ShadowRendererState& state, const GFX::Context& graphics);
bool CommitShadowMaterialDataFrame(ShadowRendererState& state, const GFX::Context& graphics);
void ActivateShadowFrameResources(ShadowRendererState& state, uint32_t frameIndex);
bool CreateShadowFrameResources(ShadowRendererState& state, const GFX::Context& graphics);
void ReleaseShadowFrameResources(ShadowRendererState& state, const GFX::Context& graphics);

struct ShadowMapAllocationDesc {
    uint32_t tileResolution = 0;
    uint32_t tilesPerRow = 2;
    bool cacheStaticDepth = true;
    const char* sourceKey = nullptr;
};
bool CreateShadowMapResources(ShadowRendererState& state, const GFX::Context& graphics,
                              const ShadowMapAllocationDesc& allocation);
void RestoreMainRenderTarget(const GFX::Context& graphics);
void PrepareFinalShadowMapForDepthWrite(ShadowRendererState& state, ID3D12GraphicsCommandList* commandList,
                                        bool clearDepth);
bool PrepareShadowMapDepthRegion(ShadowRendererState& state, ID3D12GraphicsCommandList* commandList,
                                 const D3D12_RECT& region, bool clearDepth);
void FinishFinalShadowMap(ShadowRendererState& state, const GFX::Context& graphics,
                          ID3D12GraphicsCommandList* commandList);
bool CopyStaticShadowCacheToFinal(ShadowRendererState& state, ID3D12GraphicsCommandList* commandList);
bool UpdateStaticShadowCacheFromFinal(ShadowRendererState& state, ID3D12GraphicsCommandList* commandList);

bool CreateShadowDepthPipelineState(ShadowRendererState& state, const GFX::Context& graphics);
bool CreateShadowPipelineState(ShadowRendererState& state, const GFX::Context& graphics);
bool EnsureShadowRendererInitialized(ShadowRendererState& state, const GFX::Context& graphics);

bool UploadShadowCameraConstants(ShadowRendererState& state, const GFX::Context& graphics,
                                 const ShadowLightFrame& frame, uint32_t cascadeIndex);
void SubmitShadowDebugFrustum(ShadowRendererState& state, const SceneEnvironment& environment, const Camera3D& camera);
void ClearShadowFrameSubmissions(ShadowRendererState& state);
bool PrepareShadowSourceForDraw(ShadowRendererState& state, const GFX::Context& graphics,
                                const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);
void ResetShadowGpuDrivenWorkFrame(ShadowRendererState& state, const GFX::Context& graphics);
void BuildShadowGpuDrivenWorkFrame(ShadowRendererState& state, const GFX::Context& graphics);
void UploadShadowIndirectDrawFrame(ShadowRendererState& state, const GFX::Context& graphics);
bool ExecuteShadowGpuDrivenPass(ShadowRendererState& state, const GFX::Context& graphics,
                                GFX::GPU_PROFILE::Pass traditionalProfilePass,
                                GFX::GPU_PROFILE::Pass meshletProfilePass);
bool RecordShadowView(ShadowRendererState& state, const GFX::Context& graphics, const ShadowLightFrame& frame,
                      const D3D12_RECT& region, bool clearDepth, GFX::GPU_PROFILE::Pass traditionalProfilePass,
                      GFX::GPU_PROFILE::Pass meshletProfilePass);

bool RecordWorldSpriteShadowView(ShadowRendererState& state, const GFX::Context& graphics,
                                 const ShadowLightFrame& frame, const D3D12_RECT& region, bool clearDepth);
} // namespace HIKARI::SHADOW::INTERNAL
