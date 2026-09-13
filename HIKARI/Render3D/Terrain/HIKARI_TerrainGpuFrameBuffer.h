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

namespace HIKARI::RENDER3D::TERRAIN {

    // 水/沙の sparse record と height を所有する共通 GPU バッファ。
    // 各提出のコピー元は不変。高さは version が同じ間 GPU 上で再利用する。
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
                heightCapacity_ = std::exchange(other.heightCapacity_, 0u);
            }
            return *this;
        }

        bool Initialize(ID3D12Device* device) {
            return Initialize(device, TTraits::kRecordCapacity, TTraits::kHeightCapacity);
        }

        bool Initialize(ID3D12Device* device, std::size_t recordCapacity, std::size_t heightCapacity) {
            if (device == nullptr || recordCapacity == 0u || heightCapacity == 0u ||
                recordCapacity > (std::numeric_limits<std::size_t>::max)() / sizeof(TRecord) ||
                heightCapacity > (std::numeric_limits<std::size_t>::max)() / sizeof(float)) return false;
            Finalize();
            // 未提出/拒否時は共有の不変 sentinel を参照する。既存の提出を上書きしない。
            void* neutral = nullptr;
            if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(device, sizeof(TRecord) + sizeof(float),
                    neutralBuffer_, &neutral)) return false;
            const TRecord sentinel = TTraits{}.MakeSentinel(1u);
            const float zeroHeight = 0.0f;
            std::memcpy(neutral, &sentinel, sizeof(sentinel));
            std::memcpy(static_cast<std::byte*>(neutral) + sizeof(TRecord), &zeroHeight, sizeof(zeroHeight));
            neutralBuffer_->Unmap(0, nullptr);
            for (FrameSlot& slot : slots_) {
                if (!GFX::D3D12_BUFFER::CreateDefaultBuffer(device, sizeof(TRecord) * recordCapacity, slot.recordBuffer) ||
                    !GFX::D3D12_BUFFER::CreateDefaultBuffer(device, sizeof(float) * heightCapacity, slot.heightBuffer) ||
                    !slot.uploads.Init(device, 64u * 1024u)) {
                    Finalize();
                    return false;
                }
                PublishNeutral(slot);
            }
            recordCapacity_ = recordCapacity;
            heightCapacity_ = heightCapacity;
            return true;
        }

        bool UploadFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* commandList,
            const std::vector<TRecord>* records, const std::vector<float>* heights = nullptr,
            uint64_t heightContentVersion = 0u) {
            activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
            FrameSlot* slot = ActiveSlot();
            if (slot == nullptr) return false;
            const auto publishNeutralSlot = [&]() { PublishNeutral(*slot); };
            if (commandList == nullptr) {
                publishNeutralSlot();
                return false;
            }
            const std::size_t recordCount = records != nullptr && !records->empty() ? records->size() : 1u;
            const std::size_t heightCount = heights != nullptr && !heights->empty() ? heights->size() : 1u;
            // 両方の容量/割当を確認してから GPU コマンドを記録する。
            if (recordCount > recordCapacity_ || heightCount > heightCapacity_) {
                publishNeutralSlot();
                return false;
            }
            slot->uploads.ResetForFrame(GFX::GetCurrentContext().currentFrameRetireFenceValue);
            const bool heightPayloadCurrent = heightContentVersion != 0u && slot->hasHeightContentVersion &&
                slot->heightContentVersion == heightContentVersion && slot->heightCount == heightCount;
            const bool copyRecords = recordCount > 1u;
            const bool copyHeights = heightCount > 1u && !heightPayloadCurrent;
            const auto recordUpload = copyRecords ? slot->uploads.Allocate(recordCount * sizeof(TRecord)) : DX::UploadAllocation{};
            const auto heightUpload = copyHeights ? slot->uploads.Allocate(heightCount * sizeof(float)) : DX::UploadAllocation{};
            if ((copyRecords && !recordUpload) || (copyHeights && !heightUpload)) {
                publishNeutralSlot();
                return false;
            }
            if (copyRecords) {
                const TRecord sentinel = TTraits{}.MakeSentinel(recordCount);
                std::memcpy(recordUpload.cpuAddress, &sentinel, sizeof(TRecord));
                std::memcpy(static_cast<std::byte*>(recordUpload.cpuAddress) + sizeof(TRecord),
                    records->data() + 1u, (recordCount - 1u) * sizeof(TRecord));
            }
            if (copyHeights) {
                const float zeroHeight = 0.0f;
                std::memcpy(heightUpload.cpuAddress, &zeroHeight, sizeof(float));
                std::memcpy(static_cast<std::byte*>(heightUpload.cpuAddress) + sizeof(float),
                    heights->data() + 1u, (heightCount - 1u) * sizeof(float));
            }
            if (copyRecords) RecordCopy(commandList, slot->recordBuffer.Get(), slot->recordState,
                recordUpload, recordCount * sizeof(TRecord));
            if (copyHeights) RecordCopy(commandList, slot->heightBuffer.Get(), slot->heightState,
                heightUpload, heightCount * sizeof(float));
            slot->recordAddress = copyRecords ? slot->recordBuffer->GetGPUVirtualAddress() : NeutralRecordAddress();
            slot->heightAddress = heightCount > 1u ? slot->heightBuffer->GetGPUVirtualAddress() : NeutralHeightAddress();
            slot->recordCount = recordCount;
            slot->heightCount = heightCount;
            slot->heightContentVersion = heightContentVersion;
            slot->hasHeightContentVersion = heightContentVersion != 0u;
            return true;
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetRecordGpuVirtualAddress() const {
            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr ? slot->recordAddress : 0u;
        }
        D3D12_GPU_VIRTUAL_ADDRESS GetHeightGpuVirtualAddress() const {
            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr ? slot->heightAddress : 0u;
        }
        std::size_t GetUploadedRecordCount() const {
            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr ? slot->recordCount : 0u;
        }
        std::size_t GetUploadedHeightCount() const {
            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr ? slot->heightCount : 0u;
        }
        bool IsInitialized() const {
            return GetRecordGpuVirtualAddress() != 0u && GetHeightGpuVirtualAddress() != 0u;
        }

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> recordBuffer{};
            Microsoft::WRL::ComPtr<ID3D12Resource> heightBuffer{};
            DX::DynamicUploadBuffer uploads{};
            D3D12_RESOURCE_STATES recordState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES heightState = D3D12_RESOURCE_STATE_COMMON;
            D3D12_GPU_VIRTUAL_ADDRESS recordAddress = 0u;
            D3D12_GPU_VIRTUAL_ADDRESS heightAddress = 0u;
            std::size_t recordCount = 0u;
            std::size_t heightCount = 0u;
            uint64_t heightContentVersion = 0u;
            bool hasHeightContentVersion = false;
        };

        static void RecordCopy(ID3D12GraphicsCommandList* commands, ID3D12Resource* target,
            D3D12_RESOURCE_STATES& state, const DX::UploadAllocation& source, size_t bytes) {
            const auto before = CD3DX12_RESOURCE_BARRIER::Transition(target, state, D3D12_RESOURCE_STATE_COPY_DEST);
            commands->ResourceBarrier(1, &before);
            commands->CopyBufferRegion(target, 0u, source.resource, source.offset, bytes);
            const auto after = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            commands->ResourceBarrier(1, &after);
            state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        }
        D3D12_GPU_VIRTUAL_ADDRESS NeutralRecordAddress() const {
            return neutralBuffer_ != nullptr ? neutralBuffer_->GetGPUVirtualAddress() : 0u;
        }
        D3D12_GPU_VIRTUAL_ADDRESS NeutralHeightAddress() const {
            return neutralBuffer_ != nullptr ? neutralBuffer_->GetGPUVirtualAddress() + sizeof(TRecord) : 0u;
        }
        void PublishNeutral(FrameSlot& slot) const {
            slot.recordAddress = NeutralRecordAddress();
            slot.heightAddress = NeutralHeightAddress();
            slot.recordCount = slot.heightCount = 1u;
            slot.heightContentVersion = 0u;
            slot.hasHeightContentVersion = false;
        }
        void Finalize() {
            for (FrameSlot& slot : slots_) {
                slot.uploads.Finalize(&GFX::GetCurrentContext());
                GFX::RetireD3D12ObjectForCurrentFrame(slot.recordBuffer, "SurfaceFeature.Records");
                GFX::RetireD3D12ObjectForCurrentFrame(slot.heightBuffer, "SurfaceFeature.Heights");
                slot = {};
            }
            GFX::RetireD3D12ObjectForCurrentFrame(neutralBuffer_, "SurfaceFeature.Neutral");
            activeSlotIndex_ = 0u;
            recordCapacity_ = heightCapacity_ = 0u;
        }
        FrameSlot* ActiveSlot() {
            return recordCapacity_ != 0u && heightCapacity_ != 0u ? &slots_[activeSlotIndex_] : nullptr;
        }
        const FrameSlot* ActiveSlot() const {
            return recordCapacity_ != 0u && heightCapacity_ != 0u ? &slots_[activeSlotIndex_] : nullptr;
        }

        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> neutralBuffer_{};
        uint32_t activeSlotIndex_ = 0u;
        std::size_t recordCapacity_ = 0u;
        std::size_t heightCapacity_ = 0u;
    };

} // namespace HIKARI::RENDER3D::TERRAIN
