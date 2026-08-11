#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "Gfx/HIKARI_UploadPageLayout.h"

namespace HIKARI {
    namespace DX {

        class DynamicUploadBuffer {
        public:
            DynamicUploadBuffer();
            ~DynamicUploadBuffer();

            DynamicUploadBuffer(const DynamicUploadBuffer&) = delete;
            DynamicUploadBuffer& operator=(
                const DynamicUploadBuffer&) = delete;

            bool Init(ID3D12Device* device, size_t bufferSize);

   
            void* Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress);

            void Finalize();


            void Reset();

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
        };

    } // namespace DX
} // namespace HIKARI
