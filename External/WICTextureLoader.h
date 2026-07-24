#pragma once
#include <d3d12.h>

namespace HIKARI {
    namespace DXTEX {


        HRESULT CreateWICTextureFromFile(
            ID3D12Device* device,
            ID3D12GraphicsCommandList* cmdList,
            const wchar_t* fileName,
            ID3D12Resource** texture);

        void CleanupWICResources();
    } // namespace DXTEX
} // namespace HIKARI
