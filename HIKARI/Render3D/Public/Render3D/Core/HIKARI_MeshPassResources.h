#pragma once
#include <optional>
#include "Vfx/HIKARI_VfxPostProcessMode.h"

#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/DepthOfField/HIKARI_CameraDepthOfFieldStage.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

namespace HIKARI::MESHRENDERER {

// MeshRenderer が PostSystem に直接触れないための、フレーム単位の入力テクスチャ契約。
struct MeshPassResources {
    RENDER3D::RenderTextureView sceneDepth{};
    RENDER3D::RenderTextureView opaqueSceneColor{};
    RENDER3D::RenderTextureView packedSceneSurface{};
    RENDER3D::RenderTextureView ambientOcclusion{};
    // 光線追跡の太陽の可視率（現フレーム、描画解像度）。無効なら shader はカスケード影を使う。
    RENDER3D::RenderTextureView rayTracedShadow{};
    RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
    // AS は現在フレームの render camera に一致する深度のみ消費する。
    RENDER3D::DEPTH::DepthPyramidView visibilityDepthPyramid{};
    int fallbackAoTextureHandle = -1;
    RENDER3D::PIPELINE::RenderTargetAccess compositionTarget{};
    D3D12_CPU_DESCRIPTOR_HANDLE compositionDepth{};
    const RENDER3D::DOF::WorldSpriteLayerCapture* worldSpriteDepthOfFieldCapture = nullptr;
    std::optional<VFX::VfxPostProcessMode> particlePostProcessMode;
    // 歪み層の粒子だけを戦闘の画面歪み遮罩へ描く経路
    bool particleDistortionMask = false;
    bool reflectionCapture = false;
    // 当該フレームの不透明面の深度を GeometryAux が全部書いた。Forward は深度を書かず読むだけにし、隠れた面を着色しない。
    bool opaqueDepthComplete = false;
    // Forward capture の 4 番目の出力（次表面散乱の拡散光）。reflectionCapture が前提。
    bool subsurfaceCapture = false;
};

} // namespace HIKARI::MESHRENDERER
