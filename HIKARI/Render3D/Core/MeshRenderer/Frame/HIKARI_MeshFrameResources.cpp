#include "Render3D/Core/MeshRenderer/Frame/HIKARI_MeshFrameResources.h"

#include <d3dx12.h>
#include <cstring>
#include <limits>
#include <span>

#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"

namespace HIKARI::MESHRENDERER {

    namespace {

        bool CommitStagedBufferRangesToGpu(
            ID3D12GraphicsCommandList* commandList,
            DX::DynamicUploadBuffer& uploads,
            std::span<const std::byte> staged,
            ID3D12Resource* defaultResource,
            D3D12_RESOURCE_STATES& defaultState,
            size_t elementStride,
            std::span<const RENDER3D::MATERIAL::GpuMaterialUploadRange> ranges) {

            if (ranges.empty()) return true;
            if (commandList == nullptr ||
                defaultResource == nullptr ||
                elementStride == 0u) {
                return false;
            }

            size_t uploadBytes = 0u;
            for (const auto& range : ranges) {
                const size_t capacity = staged.size() / elementStride;
                if (range.firstSlot > capacity || range.slotCount > capacity - range.firstSlot) {
                    return false;
                }
                const size_t byteCount = elementStride * range.slotCount;
                if (byteCount > (std::numeric_limits<size_t>::max)() - uploadBytes) return false;
                uploadBytes += byteCount;
            }
            if (uploadBytes == 0u) return true;
            const DX::UploadAllocation upload = uploads.Allocate(uploadBytes);
            if (!upload) return false;
            size_t packedOffset = 0u;
            for (const auto& range : ranges) {
                const size_t byteCount = elementStride * range.slotCount;
                std::memcpy(static_cast<std::byte*>(upload.cpuAddress) + packedOffset,
                    staged.data() + elementStride * range.firstSlot, byteCount);
                packedOffset += byteCount;
            }

            if (defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
                const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                    defaultResource,
                    defaultState,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                commandList->ResourceBarrier(1, &toCopyDest);
                defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
            }

            packedOffset = 0u;
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
                    upload.resource,
                    upload.offset + packedOffset,
                    byteCount);
                packedOffset += static_cast<size_t>(byteCount);
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
            return true;
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
        const UINT materialProgramParameterBytes = static_cast<UINT>(
            sizeof(MATH::Vec4) *
            RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount *
            kMaxMaterialDataCount);
        const UINT materialProgramResourceBytes = static_cast<UINT>(
            sizeof(uint32_t) *
            RENDER3D::MATERIAL::kGpuMaterialProgramResourceSlotCount *
            kMaxMaterialDataCount);
        const UINT lightBytes = GFX::AlignD3D12ConstantBufferByteSize(sizeof(LightCB));
        const UINT localLightBytes = static_cast<UINT>(
            sizeof(RENDER3D::LIGHTING::LocalLightGpuData) *
            RENDER3D::LIGHTING::kMaximumLocalLightCount);
        const UINT localLightClusterHeaderBytes = static_cast<UINT>(
            sizeof(RENDER3D::LIGHTING::LocalLightClusterHeader) *
            RENDER3D::LIGHTING::kMaximumLocalLightClusterCount);
        const UINT localLightClusterIndexBytes = static_cast<UINT>(
            sizeof(uint32_t) *
            RENDER3D::LIGHTING::
                kMaximumLocalLightClusterIndexCount);
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
            try {
                frame.materialDataStaging.resize(kMaxMaterialDataCount);
                frame.materialProgramParameterStaging.resize(
                    materialProgramParameterBytes / sizeof(MATH::Vec4));
                frame.materialProgramResourceStaging.resize(
                    materialProgramResourceBytes / sizeof(uint32_t));
            } catch (...) {
                return false;
            }
            if (!frame.materialUploads.Init(device, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) {
                return false;
            }
            frame.materialUploadNeedsRetry = false;
            frame.materialProgramUploadNeedsRetry = false;
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
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(
                    device,
                    materialDataBytes,
                    frame.materialDataBuffer) ||
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(
                    device,
                    materialProgramParameterBytes,
                    frame.materialProgramParameterBuffer) ||
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(
                    device,
                    materialProgramResourceBytes,
                    frame.materialProgramResourceBuffer) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    lightBytes,
                    frame.lightCB,
                    reinterpret_cast<void**>(&frame.lightMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    localLightBytes,
                    frame.localLightBuffer,
                    reinterpret_cast<void**>(
                        &frame.localLightMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    localLightClusterHeaderBytes,
                    frame.localLightClusterHeaderBuffer,
                    reinterpret_cast<void**>(
                        &frame.localLightClusterHeaderMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    localLightClusterIndexBytes,
                    frame.localLightClusterIndexBuffer,
                    reinterpret_cast<void**>(
                        &frame.localLightClusterIndexMapped)) ||
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
            frame.materialProgramParameterState =
                D3D12_RESOURCE_STATE_COMMON;
            frame.materialProgramResourceState =
                D3D12_RESOURCE_STATE_COMMON;

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
        Active().materialUploads.ResetForFrame(
            GFX::GetCurrentContext().currentFrameRetireFenceValue);
    }

    bool MeshFrameResourceStore::HasActiveDrawResources() const {
        const MeshFrameResources& frame = Active();
        return
            frame.objectMapped != nullptr &&
            frame.objectCB != nullptr &&
            frame.objectDataMapped != nullptr &&
            frame.objectDataBuffer != nullptr &&
            !frame.materialDataStaging.empty() &&
            frame.materialDataBuffer != nullptr &&
            !frame.materialProgramParameterStaging.empty() &&
            frame.materialProgramParameterBuffer != nullptr &&
            !frame.materialProgramResourceStaging.empty() &&
            frame.materialProgramResourceBuffer != nullptr;
    }

    bool MeshFrameResourceStore::CommitActiveMaterialRanges(
        ID3D12GraphicsCommandList* commandList,
        const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges) {

        MeshFrameResources& frame = Active();
        const RENDER3D::MATERIAL::GpuMaterialUploadRange retryRange{0, kMaxMaterialDataCount};
        // StageFrame が消費した dirty 情報は、コピー失敗時も CPU staging から復元する。
        const bool committed = CommitStagedBufferRangesToGpu(
            commandList,
            frame.materialUploads,
            std::as_bytes(std::span(frame.materialDataStaging)),
            frame.materialDataBuffer.Get(),
            frame.materialDataState,
            sizeof(MaterialGpuData),
            frame.materialUploadNeedsRetry
                ? std::span(&retryRange, 1u) : std::span(ranges));
        frame.materialUploadNeedsRetry = !committed;
        return committed;
    }

    bool MeshFrameResourceStore::CommitActiveMaterialProgramBlockRanges(
        ID3D12GraphicsCommandList* commandList,
        const std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange>& ranges) {
        MeshFrameResources& frame = Active();
        const RENDER3D::MATERIAL::GpuMaterialUploadRange retryRange{0, kMaxMaterialDataCount};
        const auto uploads = frame.materialProgramUploadNeedsRetry
            ? std::span(&retryRange, 1u) : std::span(ranges);
        const bool committed = CommitStagedBufferRangesToGpu(
            commandList,
            frame.materialUploads,
            std::as_bytes(std::span(frame.materialProgramParameterStaging)),
            frame.materialProgramParameterBuffer.Get(),
            frame.materialProgramParameterState,
            sizeof(MATH::Vec4) *
                RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount,
            uploads) && CommitStagedBufferRangesToGpu(
            commandList,
            frame.materialUploads,
            std::as_bytes(std::span(frame.materialProgramResourceStaging)),
            frame.materialProgramResourceBuffer.Get(),
            frame.materialProgramResourceState,
            sizeof(uint32_t) *
                RENDER3D::MATERIAL::kGpuMaterialProgramResourceSlotCount,
            uploads);
        frame.materialProgramUploadNeedsRetry = !committed;
        return committed;
    }

} // namespace HIKARI::MESHRENDERER
