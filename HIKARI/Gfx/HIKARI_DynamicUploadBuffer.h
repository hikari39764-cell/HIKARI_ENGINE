#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace HIKARI {
    namespace DX {

        class DynamicUploadBuffer {
        public:
            DynamicUploadBuffer();
            ~DynamicUploadBuffer();


            void Init(ID3D12Device* device, size_t bufferSize);

   
            void* Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress);

            void Finalize();


            void Reset();

        private:
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
            uint8_t* cpuBase_ = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS gpuBase_ = 0;
            size_t bufferSize_ = 0;
            size_t offset_ = 0;
        };

    } // namespace DX
} // namespace HIKARI
