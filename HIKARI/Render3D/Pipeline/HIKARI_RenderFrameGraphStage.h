#pragma once

#include <cstdint>

#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
    struct SceneEnvironment;
}

namespace HIKARI::MESHRENDERER {
    struct CameraCB;
    struct MeshPassResources;
}

namespace HIKARI::RENDER3D {
    struct RenderQualitySettings;
    namespace FEATURES {
        struct RenderFeaturePlan;
        struct RenderFeatureRequestSet;
        class StylizedScreenFeatureRenderer;
    }
    namespace LIGHTING {
        struct SceneLightingSet;
    }
    namespace SCREENSPACE {
        struct ScreenSpaceFrameResult;
    }
    namespace SURFACE {
        struct SceneSurfaceSet;
    }
}

namespace HIKARI::RENDER3D::PIPELINE {

    struct RenderFrameContext;
    struct ScreenSpacePassContext;

    struct RenderFrameGraphFeatureContext {
        const LIGHTING::SceneLightingSet& lighting;
        const FEATURES::RenderFeatureRequestSet& requests;
        FEATURES::RenderFeaturePlan& plan;
        FEATURES::StylizedScreenFeatureRenderer& renderer;
        bool enabled = true;
        bool geometryBackendReady = false;
        bool visibilityBackendReady = false;
    };

    struct RenderFrameGraphStageContext {
        const RenderFrameContext& frame;
        const ScreenSpacePassContext& screenSpace;
        const TEMPORAL::TemporalFrameState& temporalFrame;
        TEMPORAL::TemporalResourceFrame temporalResources{};
        const SceneEnvironment& environment;
        const RenderQualitySettings& renderQuality;
        RenderFrameGraphFeatureContext features;
        const MESHRENDERER::CameraCB* cameraConstants = nullptr;
        SCREENSPACE::ScreenSpaceFrameResult& screenResult;
        SURFACE::SceneSurfaceSet& sceneSurfaces;
        MESHRENDERER::MeshPassResources& opaqueResources;
        MESHRENDERER::MeshPassResources& postOpaqueResources;
        RenderDebugView debugView = RenderDebugView::None;
        bool hasDepthAwareWork = false;
        bool hasTransparentWork = false;
        bool hasSsrWork = false;
        bool requiresSceneColorOpaque = false;
    };

    struct RenderFrameGraphStageResult {
        bool prefixCompleted = false;
        bool opaqueSucceeded = false;
        bool motionVectorsWritten = false;
        bool sceneColorOpaquePublished = false;
        bool pausedForPresentation = false;
        uint32_t compiledPassCount = 0u;
        RenderFrameGraphSessionReceipt session{};
        GRAPH::RenderGraphDiagnostic diagnostic{};
    };

    MESHRENDERER::MeshPassResources BuildMeshPassResources(
        const SURFACE::SceneSurfaceSet& surfaces,
        int fallbackAoTextureHandle);

    RenderFrameGraphStageResult ExecuteRenderFrameGraphStage(
        const RenderFrameGraphStageContext& context) noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE
