#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <wrl/client.h>

#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/MeshRenderer/Resources/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshTraditionalIndirectOwner.h"
#include "Render3D/Core/MeshRenderer/Frame/HIKARI_MeshFrameResources.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "RenderCore/Resources/HIKARI_RenderResourceHandle.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Terrain/HIKARI_TerrainSurfaceGpuFrameBuffer.h"
#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Render3D/Water/HIKARI_WaterSurfaceGpuFrameBuffer.h"
#include "Render3D/Water/Spectrum/HIKARI_OceanSpectrumRuntime.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"
#include "Render3D/WorldText/HIKARI_WorldTextPass.h"

namespace HIKARI::MESHRENDERER {

struct SecondaryMeshViewFrameResources {
    MeshViewFrameResources view{};
    D3D12_GPU_VIRTUAL_ADDRESS jointPaletteAddress = 0;

    bool IsReady() const { return view.IsInitialized(); }
};

struct SecondaryMeshViewResources {
    bool initialized = false;
    // These resources own native buffers, descriptor ranges, and pass
    // objects tied to one D3D12 device.  Keep the owner identity beside
    // the initialized bit so a device-generation rollover cannot reuse
    // the old capture state.
    ID3D12Device* ownerDevice = nullptr;
    uint64_t ownerDeviceGeneration = 0u;
    std::array<SecondaryMeshViewFrameResources, GFX::kFrameResourceCount> frames{};
    std::array<RENDER3D::RenderResourceView, GFX::kFrameResourceCount> surfaceGpuSceneViews{};
    std::array<RENDER3D::RenderResourceView, GFX::kFrameResourceCount> massGpuSceneNullViews{};
    RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
    RENDER3D::WATER::WaterSurfaceGpuFrameBuffer waterSurfaceGpuFrameBuffer{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency sceneResidency{};
    RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
    RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
    RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
    RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
    RENDER3D::PARTICLES::ParticleGpuDrivenPass particlePass{};
    // このフレームの粒子の準備が通ったか（通らなかったフレームは粒子だけ描かず、キャプチャ自体は描く）。
    bool particlesPrepared = false;
    RENDER3D::WORLD_SPRITES::WorldSpritePass worldSpritePass{};
    RENDER3D::WORLD_TEXT::WorldTextPass worldTextPass{};
    bool clusterGpuDrivenReady = false;
    bool traditionalCommandStreamReady = false;
};

#if defined(HIKARI_WITH_EDITOR)
using EditorInteractiveMeshFrameResources = SecondaryMeshViewFrameResources;
using EditorInteractiveMeshResources = SecondaryMeshViewResources;
#endif

struct MeshRendererState {
    bool initialized = false;
    bool materialFrameReady = false;
    GFX::Context materialFrameContext{};
    bool initializationFailed = false;

    MeshPipelineStore pipelines;
    MeshFrameResourceStore frameResources{};

    MeshRendererDebugStats debugStats;
    // 主ビューの前向きの解析霧（このフレームの LightCB と同じ値）。画面の貼り直しが同じ霧を掛ける。
    MATH::Vec4 mainViewFogParams{};
    MATH::Vec4 mainViewFogColorDensity{};
    CameraCB frozenCullingCamera{};
    bool freezeGpuDrivenCullingCamera = false;
    bool frozenCullingCameraValid = false;
    uint32_t frozenCullingCameraWidth = 0;
    uint32_t frozenCullingCameraHeight = 0;

    RENDER3D::TextureResourceHandle fallbackTextureResource{};
    RENDER3D::TextureResourceHandle fallbackNormalTextureResource{};
    RENDER3D::TextureResourceHandle fallbackBlackTextureResource{};
    RENDER3D::TextureResourceHandle fallbackCubeTextureResource{};

    int fallbackTextureHandle = -1;
    int fallbackNormalTextureHandle = -1;
    int fallbackBlackTextureHandle = -1;
    int fallbackCubeTextureHandle = -1;

    MeshPrimitiveCache primitiveCache;
    MeshMaterialResolver materialResolver;
    size_t frameObjectIndex = 0;
    RENDER3D::MATERIAL::GpuMaterialRegistry gpuMaterialRegistry{};
    std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange> materialUploadRanges{};
    RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
    RENDER3D::GPUDRIVEN::MassGpuSceneFrameBuffer massGpuSceneBuffer{};
    RENDER3D::WATER::WaterSurfaceGpuFrameBuffer waterSurfaceGpuFrameBuffer{};
    RENDER3D::WATER::SPECTRUM::OceanSpectrumRuntime oceanSpectrumRuntime{};
    RENDER3D::TERRAIN::TerrainSurfaceGpuFrameBuffer terrainSurfaceGpuFrameBuffer{};
    RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
    MeshTraditionalIndirectOwner traditionalIndirectOwner{};
    RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
    RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
    std::array<SecondaryMeshViewResources, kSecondarySceneCaptureMaximumCount> secondaryCaptures{};
#if defined(HIKARI_WITH_EDITOR)
    EditorInteractiveMeshResources editorInteractive{};
#endif
    RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
    RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
    RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};
    RENDER3D::PARTICLES::ParticleGpuDrivenPass particlePass{};
    const RENDER3D::PARTICLES::ParticleRenderFrameSource* particleFrameSource = nullptr;
    RENDER3D::WORLD_SPRITES::WorldSpritePass worldSpritePass{};
    const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource = nullptr;
    RENDER3D::WORLD_TEXT::WorldTextPass worldTextPass{};
    const RENDER3D::WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource = nullptr;

    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource gpuDrivenSceneSource{};
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSourceIdentity = nullptr;
    RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency gpuDrivenSceneResidency{};
    bool massForwardOpaqueSubmissionCommitted = false;
};

} // namespace HIKARI::MESHRENDERER
