#pragma once

#include <d3d12.h>

#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"

namespace HIKARI::MESHRENDERER {

    // MeshRenderer が PostSystem に直接触れないための、フレーム単位の入力テクスチャ契約。
    struct MeshPassResources {
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv{};
        RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
        int fallbackAoTextureHandle = -1;
    };

} // namespace HIKARI::MESHRENDERER
