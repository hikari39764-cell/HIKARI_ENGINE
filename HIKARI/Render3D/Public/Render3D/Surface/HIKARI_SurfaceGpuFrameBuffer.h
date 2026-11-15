#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"

namespace HIKARI::RENDER3D::SURFACE {

// 面の機能の sparse record と共有 float pool を所有する GPU バッファ（水面は record、float pool の所有者は float）。
// 各提出のコピー元は不変。float pool は frame slot ごとに保持し、slot の identity と version が同じなら転送しない。
template <typename TRecord, typename TTraits>
class SurfaceGpuFrameBuffer {
public:
    SurfaceGpuFrameBuffer() = default;
    ~SurfaceGpuFrameBuffer() { Finalize(); }
    SurfaceGpuFrameBuffer(const SurfaceGpuFrameBuffer&) = delete;
    SurfaceGpuFrameBuffer& operator=(const SurfaceGpuFrameBuffer&) = delete;
    SurfaceGpuFrameBuffer(SurfaceGpuFrameBuffer&& other) noexcept { *this = std::move(other); }
    SurfaceGpuFrameBuffer& operator=(SurfaceGpuFrameBuffer&& other) noexcept {
        if (this != &other) {
            Finalize();
            slots_ = std::move(other.slots_);
            neutralBuffer_ = std::move(other.neutralBuffer_);
            activeSlotIndex_ = std::exchange(other.activeSlotIndex_, 0u);
            recordCapacity_ = std::exchange(other.recordCapacity_, 0u);
            floatCapacity_ = std::exchange(other.floatCapacity_, 0u);
        }
        return *this;
    }

    bool Initialize(ID3D12Device* device) {
        return Initialize(device, TTraits::kRecordCapacity, TTraits::kHeightCapacity);
    }

    bool Initialize(ID3D12Device* device, std::size_t recordCapacity, std::size_t floatCapacity) {
        if (device == nullptr || recordCapacity == 0u || floatCapacity == 0u ||
            recordCapacity > (std::numeric_limits<std::size_t>::max)() / sizeof(TRecord) ||
            floatCapacity > (std::numeric_limits<std::size_t>::max)() / sizeof(float))
            return false;
        Finalize();
        // 未提出/拒否時は共有の不変 sentinel を参照する。既存の提出を上書きしない。
        void* neutral = nullptr;
        if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(device, sizeof(TRecord) + sizeof(float), neutralBuffer_,
                                                         &neutral))
            return false;
        const TRecord sentinel = TTraits{}.MakeSentinel(1u);
        const float zero = 0.0f;
        std::memcpy(neutral, &sentinel, sizeof(sentinel));
        std::memcpy(static_cast<std::byte*>(neutral) + sizeof(TRecord), &zero, sizeof(zero));
        neutralBuffer_->Unmap(0, nullptr);
        for (FrameSlot& slot : slots_) {
            if (!GFX::D3D12_BUFFER::CreateDefaultBuffer(device, sizeof(TRecord) * recordCapacity,
                                                        GFX::VIDEO_MEMORY::Owner::Geometry, slot.recordBuffer) ||
                !GFX::D3D12_BUFFER::CreateDefaultBuffer(device, sizeof(float) * floatCapacity,
                                                        GFX::VIDEO_MEMORY::Owner::Geometry, slot.floatBuffer) ||
                !slot.uploads.Init(device, 64u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::SurfaceFeatures)) {
                Finalize();
                return false;
            }
            PublishNeutral(slot);
        }
        recordCapacity_ = recordCapacity;
        floatCapacity_ = floatCapacity;
        return true;
    }

    bool UploadFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* commandList, const std::vector<TRecord>* records,
                     const RUNTIME::SurfaceFeatureFloatPoolSource* floats = nullptr) {
        activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr)
            return false;
        if (commandList == nullptr) {
            PublishNeutral(*slot);
            return false;
        }
        const std::vector<float>* values = floats != nullptr ? floats->values : nullptr;
        const std::size_t recordCount = records != nullptr && !records->empty() ? records->size() : 1u;
        const std::size_t floatCount = values != nullptr && !values->empty() ? values->size() : 1u;
        // 両方の容量/割当を確認してから GPU コマンドを記録する。
        if (recordCount > recordCapacity_ || floatCount > floatCapacity_) {
            PublishNeutral(*slot);
            return false;
        }
        slot->uploads.ResetForFrame(GFX::GetCurrentContext().currentFrameRetireFenceValue);

        const bool floatsCurrent = floatCount > 1u && slot->floatsResident && slot->floatCount == floatCount &&
                                   slot->floatIdentity == floats->identity && slot->floatVersion == floats->version;
        const std::size_t uploadFloats = floatCount > 1u && !floatsCurrent ? floatCount : 0u;
        const bool copyRecords = recordCount > 1u;
        const auto recordUpload =
            copyRecords ? slot->uploads.Allocate(recordCount * sizeof(TRecord)) : DX::UploadAllocation{};
        const auto floatUpload =
            uploadFloats != 0u ? slot->uploads.Allocate(uploadFloats * sizeof(float)) : DX::UploadAllocation{};
        if ((copyRecords && !recordUpload) || (uploadFloats != 0u && !floatUpload)) {
            PublishNeutral(*slot);
            return false;
        }
        if (copyRecords) {
            const TRecord sentinel = TTraits{}.MakeSentinel(recordCount);
            std::memcpy(recordUpload.cpuAddress, &sentinel, sizeof(TRecord));
            std::memcpy(static_cast<std::byte*>(recordUpload.cpuAddress) + sizeof(TRecord), records->data() + 1u,
                        (recordCount - 1u) * sizeof(TRecord));
            RecordCopy(commandList, slot->recordBuffer.Get(), slot->recordState, recordUpload,
                       recordCount * sizeof(TRecord));
        }
        if (uploadFloats != 0u) {
            std::memcpy(floatUpload.cpuAddress, values->data(), uploadFloats * sizeof(float));
            RecordCopy(commandList, slot->floatBuffer.Get(), slot->floatState, floatUpload,
                       uploadFloats * sizeof(float));
        }
        slot->recordAddress = copyRecords ? slot->recordBuffer->GetGPUVirtualAddress() : NeutralRecordAddress();
        slot->floatAddress = floatCount > 1u ? slot->floatBuffer->GetGPUVirtualAddress() : NeutralFloatAddress();
        slot->floatCount = floatCount;
        slot->floatsResident = floatCount > 1u;
        slot->floatIdentity = floatCount > 1u ? floats->identity : 0u;
        slot->floatVersion = floatCount > 1u ? floats->version : 0u;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS GetRecordGpuVirtualAddress() const {
        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr ? slot->recordAddress : 0u;
    }
    D3D12_GPU_VIRTUAL_ADDRESS GetFloatPoolGpuVirtualAddress() const {
        const FrameSlot* slot = ActiveSlot();
        return slot != nullptr ? slot->floatAddress : 0u;
    }

private:
    struct FrameSlot {
        Microsoft::WRL::ComPtr<ID3D12Resource> recordBuffer{};
        Microsoft::WRL::ComPtr<ID3D12Resource> floatBuffer{};
        DX::DynamicUploadBuffer uploads{};
        D3D12_RESOURCE_STATES recordState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES floatState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_GPU_VIRTUAL_ADDRESS recordAddress = 0u;
        D3D12_GPU_VIRTUAL_ADDRESS floatAddress = 0u;
        std::size_t floatCount = 0u;
        uint64_t floatIdentity = 0u;
        uint64_t floatVersion = 0u;
        bool floatsResident = false;
    };

    // target の先頭へ source の bytes を写す。
    static void RecordCopy(ID3D12GraphicsCommandList* commands, ID3D12Resource* target, D3D12_RESOURCE_STATES& state,
                           const DX::UploadAllocation& source, std::size_t bytes) {
        const auto before = CD3DX12_RESOURCE_BARRIER::Transition(target, state, D3D12_RESOURCE_STATE_COPY_DEST);
        commands->ResourceBarrier(1, &before);
        commands->CopyBufferRegion(target, 0u, source.resource, source.offset, static_cast<UINT64>(bytes));
        const auto after = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        commands->ResourceBarrier(1, &after);
        state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    }
    D3D12_GPU_VIRTUAL_ADDRESS NeutralRecordAddress() const {
        return neutralBuffer_ != nullptr ? neutralBuffer_->GetGPUVirtualAddress() : 0u;
    }
    D3D12_GPU_VIRTUAL_ADDRESS NeutralFloatAddress() const {
        return neutralBuffer_ != nullptr ? neutralBuffer_->GetGPUVirtualAddress() + sizeof(TRecord) : 0u;
    }
    void PublishNeutral(FrameSlot& slot) const {
        slot.recordAddress = NeutralRecordAddress();
        slot.floatAddress = NeutralFloatAddress();
        slot.floatCount = 1u;
        slot.floatsResident = false;
        slot.floatIdentity = 0u;
        slot.floatVersion = 0u;
    }
    void Finalize() {
        for (FrameSlot& slot : slots_) {
            slot.uploads.Finalize(&GFX::GetCurrentContext());
            GFX::RetireD3D12ObjectForCurrentFrame(slot.recordBuffer, "SurfaceFeature.Records");
            GFX::RetireD3D12ObjectForCurrentFrame(slot.floatBuffer, "SurfaceFeature.Floats");
            slot = {};
        }
        GFX::RetireD3D12ObjectForCurrentFrame(neutralBuffer_, "SurfaceFeature.Neutral");
        activeSlotIndex_ = 0u;
        recordCapacity_ = floatCapacity_ = 0u;
    }
    FrameSlot* ActiveSlot() {
        return recordCapacity_ != 0u && floatCapacity_ != 0u ? &slots_[activeSlotIndex_] : nullptr;
    }
    const FrameSlot* ActiveSlot() const {
        return recordCapacity_ != 0u && floatCapacity_ != 0u ? &slots_[activeSlotIndex_] : nullptr;
    }

    std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> neutralBuffer_{};
    uint32_t activeSlotIndex_ = 0u;
    std::size_t recordCapacity_ = 0u;
    std::size_t floatCapacity_ = 0u;
};

} // namespace HIKARI::RENDER3D::SURFACE
