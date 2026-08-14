#include "Render3D/Water/HIKARI_WaterSurfaceGpuFrameBuffer.h"

#include <cstring>

#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"

namespace HIKARI::RENDER3D::WATER {

    bool WaterSurfaceGpuFrameBuffer::Initialize(
        ID3D12Device* device,
        std::size_t capacity) {

        if (device == nullptr || capacity == 0u) {
            return false;
        }
        slots_ = {};
        capacity_ = 0u;
        activeSlotIndex_ = 0u;
        const UINT64 byteSize =
            static_cast<UINT64>(sizeof(WaterSurfaceGpuData)) *
            static_cast<UINT64>(capacity);
        for (FrameSlot& slot : slots_) {
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                    device,
                    byteSize,
                    slot.buffer,
                    reinterpret_cast<void**>(&slot.mapped))) {
                slots_ = {};
                return false;
            }
            std::memset(slot.mapped, 0, static_cast<std::size_t>(byteSize));
            slot.mapped[0] = BuildWaterSurfaceGpuDataSentinel();
            slot.uploadedCount = 1u;
        }
        capacity_ = capacity;
        return true;
    }

    bool WaterSurfaceGpuFrameBuffer::UploadFrame(
        uint32_t frameIndex,
        const std::vector<WaterSurfaceGpuData>* source) {

        activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr || slot->mapped == nullptr || capacity_ == 0u) {
            return false;
        }

        const std::size_t recordCount =
            source != nullptr && !source->empty()
                ? source->size()
                : 1u;
        if (recordCount > capacity_) {
            return false;
        }
        slot->mapped[0] = BuildWaterSurfaceGpuDataSentinel(
            static_cast<uint32_t>(recordCount));
        slot->uploadedCount = 1u;
        if (recordCount <= 1u) {
            return true;
        }
        std::memcpy(
            slot->mapped + 1u,
            source->data() + 1u,
            sizeof(WaterSurfaceGpuData) * (recordCount - 1u));
        slot->uploadedCount = recordCount;
        return true;
    }

    WaterSurfaceGpuFrameBuffer::FrameSlot*
        WaterSurfaceGpuFrameBuffer::ActiveSlot() {

        return capacity_ != 0u
            ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
            : nullptr;
    }

    const WaterSurfaceGpuFrameBuffer::FrameSlot*
        WaterSurfaceGpuFrameBuffer::ActiveSlot() const {

        return capacity_ != 0u
            ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
            : nullptr;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        WaterSurfaceGpuFrameBuffer::GetGpuVirtualAddress() const {

        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr && slot->buffer != nullptr
            ? slot->buffer->GetGPUVirtualAddress()
            : 0u;
    }

    std::size_t WaterSurfaceGpuFrameBuffer::GetUploadedCount() const {
        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr ? slot->uploadedCount : 0u;
    }

    std::size_t WaterSurfaceGpuFrameBuffer::GetCapacity() const {
        return capacity_;
    }

    bool WaterSurfaceGpuFrameBuffer::IsInitialized() const {
        return capacity_ != 0u && GetGpuVirtualAddress() != 0u;
    }

} // namespace HIKARI::RENDER3D::WATER
