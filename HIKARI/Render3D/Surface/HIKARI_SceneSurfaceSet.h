#pragma once

#include <cstdint>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::SURFACE {
    // Immutable, frame-local view of the surfaces shared by screen-space
    // effects. Producers retain allocation, transition, and release ownership.
    struct SceneSurfaceSet {
        uint64_t frameIndex = 0;
        RenderViewId viewId{ kPrimaryRenderViewId };

        RenderTextureView sceneDepth{};
        RenderTextureView sceneColorOpaque{};
        RenderTextureView sceneColorPostOpaque{};
        // Reserved semantic. It remains invalid until a producer materializes
        // a sampleable pre-temporal final-color version for a declared user.
        RenderTextureView sceneColorFinal{};
        RenderTextureView packedSurface{};
        RenderTextureView surfaceFeatureMask{};
        RenderTextureView stylizedOutlineStyle{};
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

        const RenderTextureView* TryGetTexture(
            GRAPH::RenderGraphSemantic semantic) const noexcept {
            switch (semantic) {
            case GRAPH::RenderGraphSemantic::SceneDepth:
                return &sceneDepth;
            case GRAPH::RenderGraphSemantic::SceneColorOpaque:
                return &sceneColorOpaque;
            case GRAPH::RenderGraphSemantic::SceneColorPostOpaque:
                return &sceneColorPostOpaque;
            case GRAPH::RenderGraphSemantic::SceneColorFinal:
                return &sceneColorFinal;
            case GRAPH::RenderGraphSemantic::PackedSurface:
                return &packedSurface;
            case GRAPH::RenderGraphSemantic::SurfaceFeatureMask:
                return &surfaceFeatureMask;
            case GRAPH::RenderGraphSemantic::MotionVectors:
                return &motionVectors;
            case GRAPH::RenderGraphSemantic::MotionMetadata:
                return &motionMetadata;
            case GRAPH::RenderGraphSemantic::HistoryColor:
                return &historyColor;
            case GRAPH::RenderGraphSemantic::HistoryDepth:
                return &historyDepth;
            default:
                return nullptr;
            }
        }

        const DEPTH::DepthPyramidView* TryGetDepthPyramid(
            GRAPH::RenderGraphSemantic semantic) const noexcept {
            switch (semantic) {
            case GRAPH::RenderGraphSemantic::CurrentDepthPyramid:
                return &currentSceneDepthPyramid;
            case GRAPH::RenderGraphSemantic::VisibilityDepthPyramid:
                return &visibilityDepthPyramid;
            default:
                return nullptr;
            }
        }

        bool IsValid(
            GRAPH::RenderGraphSemantic semantic) const noexcept {
            if (const RenderTextureView* texture =
                    TryGetTexture(semantic)) {
                return texture->IsSampleable();
            }
            if (semantic ==
                GRAPH::RenderGraphSemantic::CurrentDepthPyramid) {
                return HasCurrentDepthHierarchy();
            }
            if (const DEPTH::DepthPyramidView* pyramid =
                    TryGetDepthPyramid(semantic)) {
                return IsDepthPyramidSampleable(*pyramid);
            }
            return false;
        }

        bool HasOpaqueSurface() const noexcept {
            return sceneDepth.IsSampleable() &&
                sceneColorOpaque.IsSampleable() &&
                packedSurface.IsSampleable();
        }

        bool HasStylizedFeatureMask() const noexcept {
            return surfaceFeatureMask.IsSampleable();
        }

        bool HasStylizedOutlineStyle() const noexcept {
            return stylizedOutlineStyle.IsSampleable() &&
                stylizedOutlineStyle.width == surfaceFeatureMask.width &&
                stylizedOutlineStyle.height == surfaceFeatureMask.height;
        }

        bool HasCurrentDepthHierarchy() const noexcept {
            return IsDepthPyramidSampleable(
                    currentSceneDepthPyramid) &&
                currentSceneDepthPyramid.viewKind ==
                    DEPTH::DepthPyramidViewKind::CurrentFrame &&
                currentSceneDepthPyramid.sourceKind ==
                    DEPTH::DepthPyramidSourceKind::SceneDepth &&
                currentSceneDepthPyramid.sourceWidth == sceneDepth.width &&
                currentSceneDepthPyramid.sourceHeight == sceneDepth.height &&
                packedSurface.width == sceneDepth.width &&
                packedSurface.height == sceneDepth.height;
        }

    private:
        static bool IsDepthPyramidSampleable(
            const DEPTH::DepthPyramidView& pyramid) noexcept {
            return pyramid.valid &&
                pyramid.viewKind !=
                    DEPTH::DepthPyramidViewKind::Invalid &&
                pyramid.sourceKind !=
                    DEPTH::DepthPyramidSourceKind::Unknown &&
                pyramid.depthConvention ==
                    DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax &&
                pyramid.pyramidSrv.ptr != 0u &&
                pyramid.mipCount != 0u &&
                pyramid.sourceWidth != 0u &&
                pyramid.sourceHeight != 0u &&
                pyramid.width != 0u &&
                pyramid.height != 0u;
        }
    };
} // namespace HIKARI::RENDER3D::SURFACE
