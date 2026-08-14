#include "Render3D/Sand/HIKARI_SandSurfaceGpuFrameBuffer.h"

#include <cstring>

#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"

namespace HIKARI::RENDER3D::SAND {

    bool SandSurfaceGpuFrameBuffer::Initialize(
        ID3D12Device* device,
        std::size_t dataCapacity,
        std::size_t heightCapacity) {

        if (device == nullptr ||
            dataCapacity == 0u ||
            heightCapacity == 0u) {
            return false;
        }
        slots_ = {};
        activeSlotIndex_ = 0u;
        dataCapacity_ = 0u;
        heightCapacity_ = 0u;

        const UINT64 dataBytes = static_cast<UINT64>(
            sizeof(SandSurfaceGpuData)) *
            static_cast<UINT64>(dataCapacity);
        const UINT64 heightBytes = static_cast<UINT64>(
            sizeof(float)) *
            static_cast<UINT64>(heightCapacity);
        for (FrameSlot& slot : slots_) {
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    dataBytes,
                    slot.dataBuffer,
                    reinterpret_cast<void**>(&slot.dataMapped)) ||
                !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    heightBytes,
                    slot.heightBuffer,
                    reinterpret_cast<void**>(&slot.heightMapped))) {
                slots_ = {};
                return false;
            }
            std::memset(
                slot.dataMapped,
                0,
                static_cast<std::size_t>(dataBytes));
            std::memset(
                slot.heightMapped,
                0,
                static_cast<std::size_t>(heightBytes));
            slot.dataMapped[0] = BuildSandSurfaceGpuDataSentinel();
        }
        dataCapacity_ = dataCapacity;
        heightCapacity_ = heightCapacity;
        return true;
    }

    bool SandSurfaceGpuFrameBuffer::UploadFrame(
        uint32_t frameIndex,
        const std::vector<SandSurfaceGpuData>* data,
        const std::vector<float>* heights) {

        activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr ||
            slot->dataMapped == nullptr ||
            slot->heightMapped == nullptr) {
            return false;
        }

        const std::size_t recordCount =
            data != nullptr && !data->empty()
                ? data->size()
                : 1u;
        if (recordCount > dataCapacity_) {
            return false;
        }
        slot->dataMapped[0] = BuildSandSurfaceGpuDataSentinel();
        slot->dataMapped[0].heightOffset =
            static_cast<uint32_t>(recordCount);
        if (recordCount > 1u) {
            std::memcpy(
                slot->dataMapped + 1u,
                data->data() + 1u,
                sizeof(SandSurfaceGpuData) * (recordCount - 1u));
        }

        const std::size_t heightCount =
            heights != nullptr && !heights->empty()
                ? heights->size()
                : 1u;
        if (heightCount > heightCapacity_) {
            slot->uploadedHeightCount = 0u;
            return false;
        }
        slot->heightMapped[0] = 0.0f;
        if (heightCount > 1u) {
            std::memcpy(
                slot->heightMapped + 1u,
                heights->data() + 1u,
                sizeof(float) * (heightCount - 1u));
        }
        slot->uploadedHeightCount = heightCount;
        return true;
    }

    SandSurfaceGpuFrameBuffer::FrameSlot*
        SandSurfaceGpuFrameBuffer::ActiveSlot() {

        return dataCapacity_ != 0u && heightCapacity_ != 0u
            ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
            : nullptr;
    }

    const SandSurfaceGpuFrameBuffer::FrameSlot*
        SandSurfaceGpuFrameBuffer::ActiveSlot() const {

        return dataCapacity_ != 0u && heightCapacity_ != 0u
            ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
            : nullptr;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        SandSurfaceGpuFrameBuffer::GetDataGpuVirtualAddress() const {

        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr && slot->dataBuffer != nullptr
            ? slot->dataBuffer->GetGPUVirtualAddress()
            : 0u;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        SandSurfaceGpuFrameBuffer::GetHeightGpuVirtualAddress() const {

        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr && slot->heightBuffer != nullptr
            ? slot->heightBuffer->GetGPUVirtualAddress()
            : 0u;
    }

    std::size_t
        SandSurfaceGpuFrameBuffer::GetUploadedHeightCount() const {

        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr ? slot->uploadedHeightCount : 0u;
    }

    bool SandSurfaceGpuFrameBuffer::IsInitialized() const {
        return dataCapacity_ != 0u &&
            heightCapacity_ != 0u &&
            GetDataGpuVirtualAddress() != 0u &&
            GetHeightGpuVirtualAddress() != 0u;
    }

} // namespace HIKARI::RENDER3D::SAND
