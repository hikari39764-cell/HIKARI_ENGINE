#include "Render3D/Pipeline/HIKARI_RenderFrameGraphStage.h"

#include <cstdio>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {
    namespace {
        RenderFrameGraphStageResult gLastStageResult{};
    }

    MESHRENDERER::MeshPassResources BuildMeshPassResources(
        const SURFACE::SceneSurfaceSet& surfaces,
        int fallbackAoTextureHandle) {
        MESHRENDERER::MeshPassResources resources{};
        resources.sceneDepth = surfaces.sceneDepth;
        resources.opaqueSceneColor = surfaces.sceneColorOpaque;
        resources.packedSceneSurface = surfaces.packedSurface;
        resources.ambientOcclusion = surfaces.ambientOcclusion;
        if (surfaces.IsValid(
                GRAPH::RenderGraphSemantic::CurrentDepthPyramid)) {
            resources.depthPyramid = surfaces.currentSceneDepthPyramid;
        }
        else if (surfaces.IsValid(
                     GRAPH::RenderGraphSemantic::
                        VisibilityDepthPyramid)) {
            resources.depthPyramid = surfaces.visibilityDepthPyramid;
        }
        resources.fallbackAoTextureHandle = fallbackAoTextureHandle;
        return resources;
    }

    RenderFrameGraphStageResult ExecuteRenderFrameGraphStage(
        const RenderFrameGraphStageContext& context) noexcept {
        INTERNAL::StageExecutionState state{};
        state.context = &context;
        if (!(context.frame.viewId == kPrimaryRenderViewId)) {
            state.result.diagnostic.code =
                GRAPH::RenderGraphResultCode::InvalidFrame;
            std::snprintf(
                state.result.diagnostic.message.data(),
                state.result.diagnostic.message.size(),
                "%s",
                "RenderFrameGraph Stage 1 accepts only the primary view.");
            TEMPORAL::MarkMotionVectorsWritten(false);
            gLastStageResult = state.result;
            return state.result;
        }
        GRAPH::RenderGraph graph{};
        const INTERNAL::RenderFrameGraphPlanOptions options{
            context.hasDepthAwareWork ||
                context.hasTransparentWork ||
                context.hasSsrWork,
            context.hasSsrWork
        };
        if (!graph.BeginFrame(
                context.frame.frameIndex,
                context.frame.viewId) ||
            !INTERNAL::BuildRenderFrameGraphPlan(
                options,
                &state,
                INTERNAL::GetRenderFrameGraphPassCallbacks(),
                graph)) {
            state.result.diagnostic = graph.GetDiagnostic();
            TEMPORAL::MarkMotionVectorsWritten(false);
            gLastStageResult = state.result;
            return state.result;
        }
        if (!graph.Compile()) {
            state.result.diagnostic = graph.GetDiagnostic();
            TEMPORAL::MarkMotionVectorsWritten(false);
            gLastStageResult = state.result;
            return state.result;
        }

        state.result.compiledPassCount = graph.GetCompiledPassCount();
        state.result.completed = graph.Execute();
        state.result.diagnostic = graph.GetDiagnostic();
        if (!state.result.motionVectorsWritten) {
            TEMPORAL::MarkMotionVectorsWritten(false);
        }
        gLastStageResult = state.result;

        static bool loggedGraphActivation = false;
        if (!loggedGraphActivation && state.result.completed) {
            loggedGraphActivation = true;
            HIKARI_LOG_INFO(
                "[RenderFrameGraph] active producer-managed graph with " +
                std::to_string(state.result.compiledPassCount) +
                " pass(es).");
        }
        return state.result;
    }

    const RenderFrameGraphStageResult&
        GetLastRenderFrameGraphStageResult() noexcept {
        return gLastStageResult;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
