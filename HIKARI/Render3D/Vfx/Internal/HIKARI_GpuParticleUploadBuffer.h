#pragma once

#include <cstddef>
#include <limits>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DXCheck.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

template <class TValue>
bool CreateGpuParticleMappedUploadBuffer(ID3D12Device* device, size_t valueCount, const char* diagnosticName,
                                         Microsoft::WRL::ComPtr<ID3D12Resource>& outResource, TValue*& outMapped) {
    outMapped = nullptr;
    if (device == nullptr || valueCount == 0u || valueCount > (std::numeric_limits<size_t>::max)() / sizeof(TValue)) {
        return false;
    }
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(valueCount * sizeof(TValue)));
    if (!HIKARI_DX_CHECK(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &description,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(outResource.GetAddressOf())),
                         diagnosticName)) {
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE noRead{0u, 0u};
    if (!HIKARI_DX_CHECK(outResource->Map(0u, &noRead, &mapped), diagnosticName)) {
        outResource.Reset();
        return false;
    }
    outMapped = static_cast<TValue*>(mapped);
    return true;
}

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
