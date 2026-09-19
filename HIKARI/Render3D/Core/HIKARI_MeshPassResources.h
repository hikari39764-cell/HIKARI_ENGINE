#pragma once

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

namespace HIKARI::MESHRENDERER {

    // MeshRenderer が PostSystem に直接触れないための、フレーム単位の入力テクスチャ契約。
    struct MeshPassResources {
        RENDER3D::RenderTextureView sceneDepth{};
        RENDER3D::RenderTextureView opaqueSceneColor{};
        RENDER3D::RenderTextureView packedSceneSurface{};
        RENDER3D::RenderTextureView ambientOcclusion{};
        RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
        // AS は現在フレームの render camera に一致する深度のみ消費する。
        RENDER3D::DEPTH::DepthPyramidView visibilityDepthPyramid{};
        int fallbackAoTextureHandle = -1;
        RENDER3D::PIPELINE::RenderTargetAccess compositionTarget{};
        D3D12_CPU_DESCRIPTOR_HANDLE compositionDepth{};
        bool reflectionCapture = false;
        bool reflectionMaterialCapture = false;
    };

} // namespace HIKARI::MESHRENDERER
