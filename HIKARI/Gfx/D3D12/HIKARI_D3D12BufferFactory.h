#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::GFX::D3D12_BUFFER {

    bool CreateMappedUploadBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        void** mappedData);

    bool CreateDefaultBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource);

    bool CreateGpuResidentMappedBuffer(
        ID3D12Device* device,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadResource,
        Microsoft::WRL::ComPtr<ID3D12Resource>& defaultResource,
        void** mappedData);

} // namespace HIKARI::GFX::D3D12_BUFFER
