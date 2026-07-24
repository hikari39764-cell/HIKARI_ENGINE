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
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::MESHRENDERER {

#if defined(HIKARI_WITH_EDITOR)
    struct EditorInteractiveMeshFrameResources {
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB{};
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB{};
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB{};
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB{};
        CameraCB* cameraMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;

        bool IsReady() const {
            return cameraCB != nullptr &&
                lightCB != nullptr &&
                shadowCB != nullptr &&
                skyEnvironmentCB != nullptr &&
                cameraMapped != nullptr &&
                lightMapped != nullptr &&
                shadowMapped != nullptr &&
                skyEnvironmentMapped != nullptr;
        }
    };

    struct EditorInteractiveMeshResources {
        bool initialized = false;
        std::array<
            EditorInteractiveMeshFrameResources,
            GFX::kFrameResourceCount> frames{};
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer
            traditionalCommandStreamBuffer{};
        std::array<
            RENDER3D::RenderResourceView,
            GFX::kFrameResourceCount> surfaceGpuSceneViews{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer
            surfaceGpuSceneBuffer{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency
            sceneResidency{};
        RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
    };
#endif

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;
        MeshFrameResourceStore frameResources{};

        MeshRendererDebugStats debugStats;
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
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
        MeshTraditionalIndirectOwner traditionalIndirectOwner{};
        RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
        RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
#if defined(HIKARI_WITH_EDITOR)
        EditorInteractiveMeshResources editorInteractive{};
#endif
        RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
        RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
        RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};

        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource gpuDrivenSceneSource{};
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSourceIdentity = nullptr;
        RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency gpuDrivenSceneResidency{};

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
