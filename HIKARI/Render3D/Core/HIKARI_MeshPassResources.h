#pragma once

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::MESHRENDERER {

    // MeshRenderer が PostSystem に直接触れないための、フレーム単位の入力テクスチャ契約。
    struct MeshPassResources {
        RENDER3D::RenderTextureView sceneDepth{};
        RENDER3D::RenderTextureView opaqueSceneColor{};
        RENDER3D::RenderTextureView packedSceneSurface{};
        RENDER3D::RenderTextureView ambientOcclusion{};
        RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
        int fallbackAoTextureHandle = -1;
        bool reflectionCapture = false;
    };

} // namespace HIKARI::MESHRENDERER
