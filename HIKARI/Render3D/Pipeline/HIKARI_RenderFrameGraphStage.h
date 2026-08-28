#pragma once

#include <cstdint>

#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Graph/HIKARI_RenderGraph.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::MESHRENDERER {
    struct CameraCB;
    struct MeshPassResources;
}

namespace HIKARI::RENDER3D {
    struct RenderQualitySettings;
    namespace SCREENSPACE {
        struct ScreenSpaceFrameResult;
    }
    namespace SURFACE {
        struct SceneSurfaceSet;
    }
    namespace TEMPORAL {
        struct TemporalFrameState;
    }
}

namespace HIKARI::RENDER3D::PIPELINE {

    struct RenderFrameContext;
    struct ScreenSpacePassContext;

    struct RenderFrameGraphStageContext {
        const RenderFrameContext& frame;
        const ScreenSpacePassContext& screenSpace;
        const TEMPORAL::TemporalFrameState& temporalFrame;
        const SceneEnvironment& environment;
        const RenderQualitySettings& renderQuality;
        const MESHRENDERER::CameraCB* cameraConstants = nullptr;
        SCREENSPACE::ScreenSpaceFrameResult& screenResult;
        SURFACE::SceneSurfaceSet& sceneSurfaces;
        MESHRENDERER::MeshPassResources& opaqueResources;
        MESHRENDERER::MeshPassResources& postOpaqueResources;
        RenderDebugView debugView = RenderDebugView::None;
        bool hasDepthAwareWork = false;
        bool hasTransparentWork = false;
        bool hasSsrWork = false;
    };

    struct RenderFrameGraphStageResult {
        bool completed = false;
        bool opaqueSucceeded = false;
        bool motionVectorsWritten = false;
        bool sceneColorOpaquePublished = false;
        uint32_t compiledPassCount = 0u;
        GRAPH::RenderGraphDiagnostic diagnostic{};
    };

    MESHRENDERER::MeshPassResources BuildMeshPassResources(
        const SURFACE::SceneSurfaceSet& surfaces,
        int fallbackAoTextureHandle);

    RenderFrameGraphStageResult ExecuteRenderFrameGraphStage(
        const RenderFrameGraphStageContext& context) noexcept;

    const RenderFrameGraphStageResult&
        GetLastRenderFrameGraphStageResult() noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE
