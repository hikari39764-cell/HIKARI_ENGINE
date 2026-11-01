#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::GFX::D3D12_BUFFER {

// upload 堆のバッファは所有者にかかわらず VRAM 記帳の upload に入る。
bool CreateMappedUploadBuffer(ID3D12Device* device, UINT64 byteSize, Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                              void** mappedData);

bool CreateDefaultBuffer(ID3D12Device* device, UINT64 byteSize, VIDEO_MEMORY::Owner owner,
                         Microsoft::WRL::ComPtr<ID3D12Resource>& resource);

bool CreateGpuResidentMappedBuffer(ID3D12Device* device, UINT64 byteSize, VIDEO_MEMORY::Owner owner,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& uploadResource,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& defaultResource, void** mappedData);

} // namespace HIKARI::GFX::D3D12_BUFFER
