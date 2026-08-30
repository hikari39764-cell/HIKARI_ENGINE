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
        if (const RenderTextureView* view = surfaces.TryGetTexture(
                GRAPH::RenderGraphSemantic::SceneDepth)) {
            resources.sceneDepth = *view;
        }
        if (const RenderTextureView* view = surfaces.TryGetTexture(
                GRAPH::RenderGraphSemantic::SceneColorOpaque)) {
            resources.opaqueSceneColor = *view;
        }
        if (const RenderTextureView* view = surfaces.TryGetTexture(
                GRAPH::RenderGraphSemantic::PackedSurface)) {
            resources.packedSceneSurface = *view;
        }
        if (const RenderTextureView* view = surfaces.TryGetTexture(
                GRAPH::RenderGraphSemantic::AmbientOcclusion)) {
            resources.ambientOcclusion = *view;
        }
        if (const DEPTH::DepthPyramidView* pyramid =
                surfaces.TryGetDepthPyramid(
                    GRAPH::RenderGraphSemantic::CurrentDepthPyramid)) {
            resources.depthPyramid = *pyramid;
        } else if (const DEPTH::DepthPyramidView* visibilityPyramid =
                surfaces.TryGetDepthPyramid(
                    GRAPH::RenderGraphSemantic::
                        VisibilityDepthPyramid)) {
            resources.depthPyramid = *visibilityPyramid;
        }
        resources.fallbackAoTextureHandle = fallbackAoTextureHandle;
        return resources;
    }

    RenderFrameGraphStageResult ExecuteRenderFrameGraphStage(
        const RenderFrameGraphStageContext& context) noexcept {
        INTERNAL::StageExecutionState state{};
        state.context = &context;
        if (!context.frame.token.MatchesFrameView(
                context.frame.frameIndex,
                context.frame.viewId) ||
            !context.sceneSurfaces.MatchesFrame(context.frame.token) ||
            context.sceneSurfaces.GetDeviceGeneration() !=
                context.frame.graphics.deviceGeneration ||
            !context.temporalResources.IsValid() ||
            !context.temporalResources.view.Matches(
                context.temporalFrame.view) ||
            !context.temporalResources.publication.Matches(
                context.frame.token) ||
            !(context.frame.viewId == kPrimaryRenderViewId)) {
            state.result.diagnostic.code =
                GRAPH::RenderGraphResultCode::InvalidFrame;
            std::snprintf(
                state.result.diagnostic.message.data(),
                state.result.diagnostic.message.size(),
                "%s",
                "RenderFrameGraph requires matching primary-view surfaces.");
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            gLastStageResult = state.result;
            return state.result;
        }
        GRAPH::RenderGraph graph{};
        const INTERNAL::RenderFrameGraphPlanOptions options{
            context.requiresSceneColorOpaque,
            context.hasSsrWork
        };
        if (!graph.BeginFrame(context.sceneSurfaces.GetFrameToken()) ||
            !INTERNAL::BuildRenderFrameGraphPlan(
                options,
                &state,
                INTERNAL::GetRenderFrameGraphPassCallbacks(),
                graph)) {
            state.result.diagnostic = graph.GetDiagnostic();
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            gLastStageResult = state.result;
            return state.result;
        }
        if (!graph.Compile()) {
            state.result.diagnostic = graph.GetDiagnostic();
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            gLastStageResult = state.result;
            return state.result;
        }

        state.result.compiledPassCount = graph.GetCompiledPassCount();
        state.result.completed = graph.Execute();
        state.result.diagnostic = graph.GetDiagnostic();
        if (!state.result.motionVectorsWritten) {
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
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
