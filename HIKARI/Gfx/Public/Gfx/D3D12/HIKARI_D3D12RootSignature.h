#pragma once
#include <d3d12.h>

namespace HIKARI::GFX {
// Root parameter layout and diagnostic context remain owned by each pass.
HRESULT CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& description,
                            ID3D12RootSignature** output);
} // namespace HIKARI::GFX
