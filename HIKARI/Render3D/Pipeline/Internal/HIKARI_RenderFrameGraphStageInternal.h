#pragma once

#include "Render3D/Pipeline/HIKARI_RenderFrameGraphStage.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

    struct StageExecutionState {
        const RenderFrameGraphStageContext* context = nullptr;
        RenderFrameGraphStageResult result{};
    };

    struct RenderFrameGraphPlanOptions {
        bool captureSceneColor = false;
        bool includeSsr = false;
    };

    struct RenderFrameGraphPassCallbacks {
        GRAPH::RenderGraphExecuteFn forwardOpaque = nullptr;
        GRAPH::RenderGraphExecuteFn postOpaqueSurfaces = nullptr;
        GRAPH::RenderGraphExecuteFn clouds = nullptr;
        GRAPH::RenderGraphExecuteFn motionVectors = nullptr;
        GRAPH::RenderGraphExecuteFn sceneColorOpaqueCapture = nullptr;
        GRAPH::RenderGraphExecuteFn ssr = nullptr;
    };

    bool ExecuteForwardOpaque(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecutePostOpaqueSurfaces(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteClouds(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteMotionVectors(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteSceneColorOpaqueCapture(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteSsr(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;

    const RenderFrameGraphPassCallbacks&
        GetRenderFrameGraphPassCallbacks() noexcept;

    bool BuildRenderFrameGraphPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        GRAPH::RenderGraph& graph) noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
