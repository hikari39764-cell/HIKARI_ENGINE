#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <d3dx12.h>

#include "HIKARI_Services.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::SHADOW::INTERNAL {

    D3D12_GPU_VIRTUAL_ADDRESS ResolveShadowJointPaletteAddress(
        size_t objectIndex) {

        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        if (frame.jointPaletteCB == nullptr ||
            objectIndex >= kMaxShadowCasterObjects) {
            return 0;
        }

        return frame.jointPaletteCB->GetGPUVirtualAddress() +
            static_cast<UINT64>(
                GFX::AlignD3D12ConstantBufferByteSize(
                    sizeof(ShadowJointPaletteCB))) *
                objectIndex;
    }

    size_t UploadShadowJointPalette(
        size_t objectIndex,
        const std::vector<MATH::Mat4>& palette) {

        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        if (frame.jointPaletteMapped == nullptr ||
            objectIndex >= kMaxShadowCasterObjects) {
            return 0;
        }

        ShadowJointPaletteCB cb{};
        for (MATH::Mat4& matrix : cb.jointMatrices) {
            matrix = MATH::Mat4::Identity();
        }
        const size_t uploadCount = std::min(palette.size(), kMaxShadowJointPaletteMatrices);
        for (size_t i = 0; i < uploadCount; ++i) {
            cb.jointMatrices[i] = palette[i];
        }

        uint8_t* dst = reinterpret_cast<uint8_t*>(frame.jointPaletteMapped) +
            static_cast<size_t>(
                GFX::AlignD3D12ConstantBufferByteSize(
                    sizeof(ShadowJointPaletteCB))) *
                objectIndex;
        std::memcpy(dst, &cb, sizeof(cb));
        return uploadCount;
    }

    void CommitMappedBufferToGpu(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* uploadResource,
        ID3D12Resource* defaultResource,
        D3D12_RESOURCE_STATES& defaultState,
        UINT64 byteCount) {

        if (commandList == nullptr ||
            uploadResource == nullptr ||
            defaultResource == nullptr ||
            byteCount == 0) {
            return;
        }

        if (defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
            const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                defaultResource,
                defaultState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &toCopyDest);
            defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        commandList->CopyBufferRegion(defaultResource, 0, uploadResource, 0, byteCount);

        const D3D12_RESOURCE_STATES shaderState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            shaderState);
        commandList->ResourceBarrier(1, &toShader);
        defaultState = shaderState;
    }

    void CommitShadowMaterialDataFrame(ID3D12GraphicsCommandList* commandList) {
        ShadowFrameResources& frame = GetActiveShadowFrameResources();
        const UINT64 materialBytes =
            static_cast<UINT64>(sizeof(MESHRENDERER::MaterialGpuData)) *
            static_cast<UINT64>(
                (std::min)(
                    static_cast<size_t>(gShadowRendererState.materialDataFrameTable.count),
                    static_cast<size_t>(MESHRENDERER::kMaxMaterialDataCount)));
        if (materialBytes == 0u) {
            return;
        }

        CommitMappedBufferToGpu(
            commandList,
            frame.materialDataUploadBuffer.Get(),
            frame.materialDataBuffer.Get(),
            frame.materialDataState,
            materialBytes);
    }

    void ActivateShadowFrameResources(uint32_t frameIndex) {
        gShadowRendererState.activeFrameResourceIndex = frameIndex % GFX::kFrameResourceCount;
    }

    bool CreateShadowFrameResources(ID3D12Device* device) {
        const UINT cameraBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowCameraCB));
        const UINT materialDataBytes =
            static_cast<UINT>(sizeof(MESHRENDERER::MaterialGpuData) * MESHRENDERER::kMaxMaterialDataCount);
        const UINT paletteBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowJointPaletteCB)) * kMaxShadowCasterObjects;

        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        if (srvHeap == nullptr) {
            return false;
        }
        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT surfaceGpuSceneSrvIndex =
            GFX::DESCRIPTOR::ToFrameIndex(
                GFX::DESCRIPTOR::SystemSrv::ShadowSurfaceGpuSceneFrame0,
                0u);
        const D3D12_CPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvCpu =
            GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);
        const D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrvGpu =
            GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, surfaceGpuSceneSrvIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
        materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
        materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        materialDataSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        materialDataSrv.Buffer.FirstElement = 0;
        materialDataSrv.Buffer.NumElements = MESHRENDERER::kMaxMaterialDataCount;
        materialDataSrv.Buffer.StructureByteStride = sizeof(MESHRENDERER::MaterialGpuData);
        materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        if (!gShadowRendererState.surfaceGpuSceneBuffer.Initialize(
            device,
            surfaceGpuSceneSrvCpu,
            surfaceGpuSceneSrvGpu,
            descriptorSize)) {
            DEBUGLOG::PushRenderError("[ShadowMapRenderer][WARN] SurfaceGpuScene buffer initialization failed. GPU-driven shadow pass will be unavailable.");
        }

        for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
            ShadowFrameResources& frame = gShadowRendererState.frameResources[frameIndex];
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                device,
                cameraBytes,
                frame.cameraCB,
                reinterpret_cast<void**>(&frame.cameraMapped)) ||
                !GFX::D3D12_BUFFER::CreateGpuResidentMappedBuffer(
                    device,
                    materialDataBytes,
                    frame.materialDataUploadBuffer,
                    frame.materialDataBuffer,
                    reinterpret_cast<void**>(&frame.materialDataMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    paletteBytes,
                    frame.jointPaletteCB,
                    reinterpret_cast<void**>(&frame.jointPaletteMapped))) {
                return false;
            }
            frame.materialDataState = D3D12_RESOURCE_STATE_COMMON;

            const UINT materialDataSrvIndex =
                GFX::DESCRIPTOR::ToFrameIndex(
                    GFX::DESCRIPTOR::SystemSrv::ShadowMaterialDataFrame0,
                    frameIndex);
            const D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
            frame.materialDataSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, materialDataSrvIndex);
            device->CreateShaderResourceView(
                frame.materialDataBuffer.Get(),
                &materialDataSrv,
                materialDataSrvCpu);
            GFX::SetD3D12Name(frame.materialDataBuffer.Get(), L"Shadow MaterialData Buffer");
        }

        ActivateShadowFrameResources(SERVICES::gCtx.frameIndex);
        return true;
    }

} // namespace HIKARI::SHADOW::INTERNAL
