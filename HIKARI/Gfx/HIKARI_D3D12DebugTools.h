#pragma once

#include <d3d12.h>

namespace HIKARI::GFX {

    void ConfigureD3D12InfoQueue(ID3D12Device* device);
    void DumpD3D12InfoQueue(ID3D12Device* device, const char* reason);
    void ClearD3D12InfoQueue(ID3D12Device* device);
    void DumpDxgiInfoQueue(const char* reason);
    void ClearDxgiInfoQueue();

}
