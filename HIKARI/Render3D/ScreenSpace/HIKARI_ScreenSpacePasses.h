#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"
#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::SCREENSPACE {

    enum class DepthVisibilitySource : uint32_t {
        None,
        History,
        Frozen,
        CurrentPrepass,
        CurrentSceneDepth,
        NoHzb,
    };

    struct DepthVisibilityDebugState {
        uint32_t width = 0;
        uint32_t height = 0;
        bool historyReady = false;
        bool historyMatched = false;
        bool depthPrepassWritten = false;
        bool depthPyramidBuilt = false;
        bool visibilityUsedHzb = false;
        bool visibilityWithoutHzb = false;
        DepthVisibilitySource visibilitySource = DepthVisibilitySource::None;
        DepthVisibilitySource latestPyramidSource = DepthVisibilitySource::None;
    };

    struct ScreenSpaceRuntimeState {
        ScreenSpaceGeometryAux geometryAux{};
        SsaoRenderer ssaoRenderer{};
        RENDER3D::GPUDRIVEN::GpuDepthVisibilityLayer depthVisibility{};
        RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats frozenCullingDepthStats{};
        MATH::Mat4 depthVisibilityViewProj{};
        RENDER3D::TextureResourceHandle fallbackAoTextureResource{};
        int fallbackAoTextureHandle = -1;
        bool depthVisibilityValid = false;
        bool depthVisibilityViewProjValid = false;
        bool depthVisibilityBuildAllowedThisFrame = true;
        bool frozenCullingDepthStatsValid = false;
        bool geometryValid = false;
        bool ssaoValid = false;
        // Balanced SSAO を scene depth prepass の深度から当該フレーム内で
        // 描画済みか。true の間は post-opaque の temporal 更新をスキップする。
        bool balancedSsaoSameFrame = false;
    };

    struct ScreenSpaceFrameResult {
        bool depthPrepassWritten = false;
        bool hzbBuilt = false;
        bool depthPyramidBuilt = false;
        bool geometryAuxWritten = false;
        bool ssaoRendered = false;
        RENDER3D::DEPTH::DepthPyramidView depthPyramid{};
        D3D12_GPU_DESCRIPTOR_HANDLE aoSrv{};
        RENDER3D::TextureResourceHandle fallbackAoTextureResource{};
        int fallbackAoTextureHandle = -1;
    };

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState();
    const DepthVisibilityDebugState& GetDepthVisibilityDebugState();
    const char* ToString(DepthVisibilitySource source);
    void ReleaseScreenSpaceRuntimeState();

    bool EnsureScreenSpaceFallbacks(ScreenSpaceRuntimeState& state);

    ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& renderCameraCb,
        const MESHRENDERER::CameraCB& cullingCameraCb,
        const SceneEnvironment& environment);
    bool ExecuteScreenSpacePostOpaquePasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment,
        ScreenSpaceFrameResult& result);

    // Scene depth prepass 完了後に Balanced SSAO を当該フレームの深度から描く。
    // 旧 temporal 経路 (1 フレーム遅れ・再投影なし) の移動時の引き摺りを解消
    // する。prepass が走らないフレームでは呼ばれず、従来経路が使われる。
    bool ExecuteBalancedSsaoFromSceneDepth(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& renderCameraCb,
        const SceneEnvironment& environment,
        ScreenSpaceFrameResult& result);

} // namespace HIKARI::RENDER3D::SCREENSPACE
