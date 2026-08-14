#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SURFACE {
    // Immutable, frame-local view of the surfaces shared by screen-space
    // effects. Producers retain allocation, transition, and release ownership.
    struct SceneSurfaceSet {
        uint64_t frameIndex = 0;
        RenderViewId viewId{ kPrimaryRenderViewId };

        RenderTextureView sceneDepth{};
        RenderTextureView opaqueSceneColor{};
        RenderTextureView packedSurface{};
        RenderTextureView surfaceFeatureMask{};
        RenderTextureView ambientOcclusion{};
        RenderTextureView motionVectors{};
        RenderTextureView motionMetadata{};
        RenderTextureView reactiveMask{};
        RenderTextureView transparencyMask{};
        RenderTextureView invalidDepthMotionMask{};
        RenderTextureView historyColor{};
        RenderTextureView historyDepth{};
        // Lighting reads current depth; visibility may read history/frozen depth.
        DEPTH::DepthPyramidView currentSceneDepthPyramid{};
        DEPTH::DepthPyramidView visibilityDepthPyramid{};

        bool HasOpaqueSurface() const noexcept {
            return sceneDepth.IsSampleable() &&
                opaqueSceneColor.IsSampleable() &&
                packedSurface.IsSampleable();
        }

        bool HasStylizedFeatureMask() const noexcept {
            return surfaceFeatureMask.IsSampleable();
        }

        bool HasCurrentDepthHierarchy() const noexcept {
            return currentSceneDepthPyramid.valid &&
                currentSceneDepthPyramid.viewKind ==
                    DEPTH::DepthPyramidViewKind::CurrentFrame &&
                currentSceneDepthPyramid.sourceKind ==
                    DEPTH::DepthPyramidSourceKind::SceneDepth &&
                currentSceneDepthPyramid.depthConvention ==
                    DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax &&
                currentSceneDepthPyramid.pyramidSrv.ptr != 0 &&
                currentSceneDepthPyramid.mipCount != 0u &&
                currentSceneDepthPyramid.width != 0u &&
                currentSceneDepthPyramid.height != 0u &&
                currentSceneDepthPyramid.sourceWidth == sceneDepth.width &&
                currentSceneDepthPyramid.sourceHeight == sceneDepth.height &&
                packedSurface.width == sceneDepth.width &&
                packedSurface.height == sceneDepth.height;
        }
    };
} // namespace HIKARI::RENDER3D::SURFACE
