#pragma once

#include <span>

#include <d3d12.h>

namespace HIKARI::MESHRENDERER {

    std::span<const D3D12_INPUT_ELEMENT_DESC> GetStaticMeshInputLayout();
    std::span<const D3D12_INPUT_ELEMENT_DESC> GetSkinnedMeshInputLayout();

} // namespace HIKARI::MESHRENDERER
