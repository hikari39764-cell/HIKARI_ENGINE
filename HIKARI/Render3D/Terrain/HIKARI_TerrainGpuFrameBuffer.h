#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"

namespace HIKARI::RENDER3D::TERRAIN {

    // 地表 feature の sparse GPU record を per-frame でリングアップロードする
    // 共通バッファ。record と任意の float height field を同時に保持する。
    // TTraits は容量と sentinel 構築を提供し、水/沙などで実装を共有する。
    template <typename TRecord, typename TTraits>
    class SurfaceGpuFrameBuffer {
    public:
        bool Initialize(ID3D12Device* device) {
            return Initialize(
                device,
                TTraits::kRecordCapacity,
                TTraits::kHeightCapacity);
        }

        bool Initialize(
            ID3D12Device* device,
            std::size_t recordCapacity,
            std::size_t heightCapacity) {

            if (device == nullptr ||
                recordCapacity == 0u ||
                heightCapacity == 0u) {
                return false;
            }
            slots_ = {};
            recordCapacity_ = 0u;
            heightCapacity_ = 0u;
            activeSlotIndex_ = 0u;

            const UINT64 recordBytes =
                static_cast<UINT64>(sizeof(TRecord)) *
                static_cast<UINT64>(recordCapacity);
            const UINT64 heightBytes =
                static_cast<UINT64>(sizeof(float)) *
                static_cast<UINT64>(heightCapacity);
            for (FrameSlot& slot : slots_) {
                if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                        device,
                        recordBytes,
                        slot.recordBuffer,
                        reinterpret_cast<void**>(&slot.recordMapped)) ||
                    !GFX::D3D12_BUFFER::CreateMappedUploadBuffer(
                        device,
                        heightBytes,
                        slot.heightBuffer,
                        reinterpret_cast<void**>(&slot.heightMapped))) {
                    slots_ = {};
                    return false;
                }
                std::memset(
                    slot.recordMapped,
                    0,
                    static_cast<std::size_t>(recordBytes));
                std::memset(
                    slot.heightMapped,
                    0,
                    static_cast<std::size_t>(heightBytes));
                slot.recordMapped[0] = TTraits{}.MakeSentinel(1u);
                slot.heightMapped[0] = 0.0f;
                slot.recordCount = 1u;
                slot.heightCount = 1u;
            }
            recordCapacity_ = recordCapacity;
            heightCapacity_ = heightCapacity;
            return true;
        }

        bool UploadFrame(
            uint32_t frameIndex,
            const std::vector<TRecord>* records,
            const std::vector<float>* heights = nullptr) {

            activeSlotIndex_ =
                frameIndex % GFX::kFrameResourceCount;
            FrameSlot* slot = ActiveSlot();
            if (slot == nullptr ||
                slot->recordMapped == nullptr ||
                slot->heightMapped == nullptr) {
                return false;
            }

            const std::size_t recordCount =
                records != nullptr && !records->empty()
                    ? records->size()
                    : 1u;
            const std::size_t heightCount =
                heights != nullptr && !heights->empty()
                    ? heights->size()
                    : 1u;
            const auto publishNeutralSlot = [&]() {
                slot->recordMapped[0] = TTraits{}.MakeSentinel(1u);
                slot->heightMapped[0] = 0.0f;
                slot->recordCount = 1u;
                slot->heightCount = 1u;
            };
            // 容量を両方検証してから一文字も payload を書かない。失敗 slot は
            // sentinel のみに戻し、以前の record と新しい offset を混在させない。
            if (recordCount > recordCapacity_ ||
                heightCount > heightCapacity_) {
                publishNeutralSlot();
                return false;
            }

            slot->recordMapped[0] =
                TTraits{}.MakeSentinel(recordCount);
            slot->recordCount = 1u;
            if (recordCount > 1u) {
                std::memcpy(
                    slot->recordMapped + 1u,
                    records->data() + 1u,
                    sizeof(TRecord) * (recordCount - 1u));
                slot->recordCount = recordCount;
            }

            slot->heightMapped[0] = 0.0f;
            slot->heightCount = 1u;
            if (heightCount > 1u) {
                std::memcpy(
                    slot->heightMapped + 1u,
                    heights->data() + 1u,
                    sizeof(float) * (heightCount - 1u));
                slot->heightCount = heightCount;
            }
            return true;
        }

        D3D12_GPU_VIRTUAL_ADDRESS
            GetRecordGpuVirtualAddress() const {

            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr && slot->recordBuffer != nullptr
                ? slot->recordBuffer->GetGPUVirtualAddress()
                : 0u;
        }

        D3D12_GPU_VIRTUAL_ADDRESS
            GetHeightGpuVirtualAddress() const {

            const FrameSlot* slot = ActiveSlot();
            return slot != nullptr && slot->heightBuffer != nullptr
                ? slot->heightBuffer->GetGPUVirtualAddress()
                : 0u;
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
            return recordCapacity_ != 0u &&
                heightCapacity_ != 0u &&
                GetRecordGpuVirtualAddress() != 0u &&
                GetHeightGpuVirtualAddress() != 0u;
        }

    private:
        struct FrameSlot {
            Microsoft::WRL::ComPtr<ID3D12Resource> recordBuffer{};
            TRecord* recordMapped = nullptr;
            Microsoft::WRL::ComPtr<ID3D12Resource> heightBuffer{};
            float* heightMapped = nullptr;
            std::size_t recordCount = 0u;
            std::size_t heightCount = 0u;
        };

        FrameSlot* ActiveSlot() {
            return recordCapacity_ != 0u && heightCapacity_ != 0u
                ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
                : nullptr;
        }

        const FrameSlot* ActiveSlot() const {
            return recordCapacity_ != 0u && heightCapacity_ != 0u
                ? &slots_[activeSlotIndex_ % GFX::kFrameResourceCount]
                : nullptr;
        }

        std::array<FrameSlot, GFX::kFrameResourceCount> slots_{};
        uint32_t activeSlotIndex_ = 0u;
        std::size_t recordCapacity_ = 0u;
        std::size_t heightCapacity_ = 0u;
    };

} // namespace HIKARI::RENDER3D::TERRAIN
