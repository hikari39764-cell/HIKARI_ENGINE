#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"

namespace HIKARI {
    class Mesh;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::RENDER3D::RUNTIME {
    struct SurfaceDrawCommand;
    struct SurfaceGpuSceneInstance;
    struct SurfaceGpuSceneMaterialSource;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuSceneSurfaceRecord;
    class SurfaceGpuSceneFrameBuffer;
    class GpuTraditionalCommandStreamBuffer;
}

namespace HIKARI::MESHRENDERER {

    class MeshMaterialResolver;
    class MeshPrimitiveCache;
    struct MeshPipelineStore;

    struct MeshDrawServices {
        ID3D12Device* device = nullptr;
        MeshPrimitiveCache* primitiveCache = nullptr;
        MeshMaterialResolver* materialResolver = nullptr;
        MeshPipelineStore* pipelines = nullptr;
        MeshRendererDebugStats* stats = nullptr;
    };

    enum class MeshDrawPassKind {
        Forward,
        GeometryAux,
        DepthPrepass,
    };

    struct MeshDrawContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        ID3D12RootSignature* staticRootSig = nullptr;
        ID3D12RootSignature* skinnedRootSig = nullptr;
        ID3D12Resource* objectCB = nullptr;
        ID3D12Resource* objectDataBuffer = nullptr;
        ID3D12Resource* materialDataBuffer = nullptr;
        ID3D12Resource* jointPaletteCB = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;
        RENDER3D::MATERIAL::GpuMaterialRegistry* gpuMaterialRegistry = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer* surfaceGpuSceneFrameBuffer = nullptr;
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer* traditionalCommandStreamBuffer = nullptr;
        size_t surfaceGpuSceneBaseOffset = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
        MeshDrawPassKind passKind = MeshDrawPassKind::Forward;
        MeshBindingContext binding{};
        MeshMaterialFillContext materialFill{};
        MeshDrawServices services{};
    };

    void BindSurfaceRecordFrameResources(const MeshDrawContext& ctx);

    bool PrepareSurfaceGpuSceneMaterialSources(
        const MeshDrawContext& ctx,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sources,
        size_t sourceCount);

} // namespace HIKARI::MESHRENDERER
