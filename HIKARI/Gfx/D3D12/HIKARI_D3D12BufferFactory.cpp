#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"

#include <cstring>

#include <d3dx12.h>

namespace HIKARI::GFX::D3D12_BUFFER {

    bool CreateMappedUploadBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        void** mappedData) {

        if (device == nullptr || byteSize == 0 || mappedData == nullptr) {
            return false;
        }

        resource.Reset();
        *mappedData = nullptr;
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf())))) {
            return false;
        }

        return SUCCEEDED(resource->Map(0, nullptr, mappedData));
    }

    bool CreateDefaultBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {

        if (device == nullptr || byteSize == 0) {
            return false;
        }

        resource.Reset();
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        return SUCCEEDED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(resource.GetAddressOf())));
    }

    bool CreateGpuResidentMappedBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadResource,
        Microsoft::WRL::ComPtr<ID3D12Resource>& defaultResource,
        void** mappedData) {

        if (!CreateMappedUploadBuffer(
                device,
                byteSize,
                uploadResource,
                mappedData) ||
            !CreateDefaultBuffer(device, byteSize, defaultResource)) {
            return false;
        }

        if (mappedData != nullptr && *mappedData != nullptr) {
            std::memset(*mappedData, 0, static_cast<size_t>(byteSize));
        }
        return true;
    }

} // namespace HIKARI::GFX::D3D12_BUFFER
