#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "Gfx/HIKARI_UploadPageLayout.h"

namespace HIKARI {
    namespace GFX { struct Context; }
    namespace DX {

        // ページ所有者が保持する一回分の領域。Fence 完了後の Reset まで再利用されない。
        struct UploadAllocation {
            void* cpuAddress = nullptr;
            ID3D12Resource* resource = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0u;
            size_t offset = 0u;
            explicit operator bool() const noexcept { return cpuAddress != nullptr; }
        };

        class DynamicUploadBuffer {
        public:
            DynamicUploadBuffer();
            ~DynamicUploadBuffer();

            DynamicUploadBuffer(const DynamicUploadBuffer&) = delete;
            DynamicUploadBuffer& operator=(
                const DynamicUploadBuffer&) = delete;
            DynamicUploadBuffer(DynamicUploadBuffer&&) noexcept = default;
            DynamicUploadBuffer& operator=(DynamicUploadBuffer&& other) noexcept;

            bool Init(ID3D12Device* device, size_t bufferSize);

            UploadAllocation Allocate(size_t size);
            void* Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress);

            // 記録済みコマンドが参照するページは、指定フレームの Fence まで保持する。
            void Finalize(const GFX::Context* retireContext = nullptr);


            void Reset();

            // 呼び出し側は対象スロットの GPU 完了を保証する。同一フレーム内では巻き戻さない。
            void ResetForFrame(uint64_t frameSerial);
            bool IsInitialized() const { return device_ != nullptr; }

        private:
            struct Page {
                Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
                uint8_t* cpuBase = nullptr;
                D3D12_GPU_VIRTUAL_ADDRESS gpuBase = 0u;
            };

            bool CreatePage(size_t capacity);

            Microsoft::WRL::ComPtr<ID3D12Device> device_{};
            std::vector<Page> pages_{};
            std::vector<GFX::UploadPageRange> pageRanges_{};
            size_t defaultPageSize_ = 0u;
            size_t activePageIndex_ = 0u;
            uint64_t frameSerial_ = 0u;
            bool hasFrameSerial_ = false;
        };

    } // namespace DX
} // namespace HIKARI
