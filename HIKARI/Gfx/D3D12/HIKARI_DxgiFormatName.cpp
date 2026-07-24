#include "Gfx/D3D12/HIKARI_DxgiFormatName.h"

namespace HIKARI::GFX {

    const char* DxgiFormatName(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return "R11G11B10_FLOAT";
        case DXGI_FORMAT_BC6H_UF16:
            return "BC6H_UF16";
        case DXGI_FORMAT_BC6H_SF16:
            return "BC6H_SF16";
        default:
            return "DXGI_FORMAT_OTHER";
        }
    }

} // namespace HIKARI::GFX
