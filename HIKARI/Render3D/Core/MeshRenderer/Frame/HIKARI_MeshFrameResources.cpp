#include "Render3D/Core/MeshRenderer/Frame/HIKARI_MeshFrameResources.h"

#include <d3dx12.h>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"

namespace HIKARI::MESHRENDERER {

    namespace {

        void CommitMappedBufferRangesToGpu(
            ID3D12GraphicsCommandList* commandList,
            ID3D12Resource* uploadResource,
            ID3D12Resource* defaultResource,
            D3D12_RESOURCE_STATES& defaultState,
            size_t elementStride,
            const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges) {

            if (commandList == nullptr ||
                uploadResource == nullptr ||
                defaultResource == nullptr ||
                elementStride == 0u ||
                ranges.empty()) {
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

            for (const RENDER3D::MATERIAL::GpuMaterialUploadRange& range : ranges) {
                if (range.slotCount == 0u) {
                    continue;
                }
                const UINT64 byteOffset =
                    static_cast<UINT64>(elementStride) * range.firstSlot;
                const UINT64 byteCount =
                    static_cast<UINT64>(elementStride) * range.slotCount;
                commandList->CopyBufferRegion(
                    defaultResource,
                    byteOffset,
                    uploadResource,
                    byteOffset,
                    byteCount);
            }

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

    } // namespace

    bool MeshFrameResourceStore::Initialize(
        ID3D12Device* device,
        ID3D12DescriptorHeap* srvHeap) {

        if (device == nullptr || srvHeap == nullptr) {
            return false;
        }

        const UINT cameraBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(CameraCB));
        const UINT objectBytes =
            GFX::AlignD3D12ConstantBufferByteSize(sizeof(ObjectCB)) * kMaxObjectCount;
        const UINT objectDataBytes =
            static_cast<UINT>(sizeof(ObjectGpuData) * kMaxObjectCount);
        const UINT materialDataBytes =
            static_cast<UINT>(sizeof(MaterialGpuData) * kMaxMaterialDataCount);
        const UINT lightBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(LightCB));
        const UINT shadowBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(ShadowCB));
        const UINT skyEnvironmentBytes =
            GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkyEnvironmentCB));
        const UINT jointPaletteBytes =
            GFX::AlignD3D12ConstantBufferByteSize(sizeof(JointPaletteCB)) * kMaxObjectCount;

        const UINT descriptorSize =
            device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC objectDataSrv{};
        objectDataSrv.Format = DXGI_FORMAT_UNKNOWN;
        objectDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        objectDataSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        objectDataSrv.Buffer.FirstElement = 0;
        objectDataSrv.Buffer.NumElements = kMaxObjectCount;
        objectDataSrv.Buffer.StructureByteStride = sizeof(ObjectGpuData);
        objectDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_SHADER_RESOURCE_VIEW_DESC materialDataSrv{};
        materialDataSrv.Format = DXGI_FORMAT_UNKNOWN;
        materialDataSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        materialDataSrv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        materialDataSrv.Buffer.FirstElement = 0;
        materialDataSrv.Buffer.NumElements = kMaxMaterialDataCount;
        materialDataSrv.Buffer.StructureByteStride = sizeof(MaterialGpuData);
        materialDataSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        for (uint32_t frameIndex = 0;
            frameIndex < GFX::kFrameResourceCount;
            ++frameIndex) {

            MeshFrameResources& frame = frames_[frameIndex];
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    cameraBytes,
                    frame.cameraCB,
                    reinterpret_cast<void**>(&frame.cameraMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    cameraBytes,
                    frame.cullingCameraCB,
                    reinterpret_cast<void**>(&frame.cullingCameraMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    objectBytes,
                    frame.objectCB,
                    reinterpret_cast<void**>(&frame.objectMapped)) ||
                !GFX::D3D12_BUFFER::CreateGpuResidentMappedBuffer(
                    device,
                    objectDataBytes,
                    frame.objectDataUploadBuffer,
                    frame.objectDataBuffer,
                    reinterpret_cast<void**>(&frame.objectDataMapped)) ||
                !GFX::D3D12_BUFFER::CreateGpuResidentMappedBuffer(
                    device,
                    materialDataBytes,
                    frame.materialDataUploadBuffer,
                    frame.materialDataBuffer,
                    reinterpret_cast<void**>(&frame.materialDataMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    lightBytes,
                    frame.lightCB,
                    reinterpret_cast<void**>(&frame.lightMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    shadowBytes,
                    frame.shadowCB,
                    reinterpret_cast<void**>(&frame.shadowMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    skyEnvironmentBytes,
                    frame.skyEnvironmentCB,
                    reinterpret_cast<void**>(&frame.skyEnvironmentMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    jointPaletteBytes,
                    frame.jointPaletteCB,
                    reinterpret_cast<void**>(&frame.jointPaletteMapped))) {
                return false;
            }

            frame.objectDataState = D3D12_RESOURCE_STATE_COMMON;
            frame.materialDataState = D3D12_RESOURCE_STATE_COMMON;

            const UINT objectDataSrvIndex = GFX::DESCRIPTOR::ToFrameIndex(
                GFX::DESCRIPTOR::SystemSrv::MeshObjectDataFrame0,
                frameIndex);
            frame.objectDataSrvCpu = GFX::DESCRIPTOR::CpuAt(
                srvHeap,
                descriptorSize,
                objectDataSrvIndex);
            frame.objectDataSrvGpu = GFX::DESCRIPTOR::GpuAt(
                srvHeap,
                descriptorSize,
                objectDataSrvIndex);
            device->CreateShaderResourceView(
                frame.objectDataBuffer.Get(),
                &objectDataSrv,
                frame.objectDataSrvCpu);

            const UINT materialDataSrvIndex = GFX::DESCRIPTOR::ToFrameIndex(
                GFX::DESCRIPTOR::SystemSrv::MeshMaterialDataFrame0,
                frameIndex);
            frame.materialDataSrvCpu = GFX::DESCRIPTOR::CpuAt(
                srvHeap,
                descriptorSize,
                materialDataSrvIndex);
            frame.materialDataSrvGpu = GFX::DESCRIPTOR::GpuAt(
                srvHeap,
                descriptorSize,
                materialDataSrvIndex);
            device->CreateShaderResourceView(
                frame.materialDataBuffer.Get(),
                &materialDataSrv,
                frame.materialDataSrvCpu);
        }

        Activate(0);
        return true;
    }

    void MeshFrameResourceStore::Activate(uint32_t frameIndex) {
        activeFrameIndex_ = frameIndex % GFX::kFrameResourceCount;
    }

    bool MeshFrameResourceStore::HasActiveDrawResources() const {
        const MeshFrameResources& frame = Active();
        return
            frame.objectMapped != nullptr &&
            frame.objectCB != nullptr &&
            frame.objectDataMapped != nullptr &&
            frame.objectDataBuffer != nullptr &&
            frame.materialDataMapped != nullptr &&
            frame.materialDataBuffer != nullptr;
    }

    void MeshFrameResourceStore::CommitActiveMaterialRanges(
        ID3D12GraphicsCommandList* commandList,
        size_t elementStride,
        const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges) {

        MeshFrameResources& frame = Active();
        CommitMappedBufferRangesToGpu(
            commandList,
            frame.materialDataUploadBuffer.Get(),
            frame.materialDataBuffer.Get(),
            frame.materialDataState,
            elementStride,
            ranges);
    }

} // namespace HIKARI::MESHRENDERER
