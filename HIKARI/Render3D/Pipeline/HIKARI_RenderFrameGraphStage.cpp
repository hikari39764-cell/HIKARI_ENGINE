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
        RenderFrameGraphStageResult rejected{};
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
            rejected.diagnostic.code =
                GRAPH::RenderGraphResultCode::InvalidFrame;
            std::snprintf(
                rejected.diagnostic.message.data(),
                rejected.diagnostic.message.size(),
                "%s",
                "RenderFrameGraph requires matching primary-view surfaces.");
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            return rejected;
        }

        RenderFrameGraphSessionReceipt session{};
        if (!BeginRenderFrameGraphSession(
                context.frame.surfaceReceipt,
                session)) {
            rejected.diagnostic.code =
                GRAPH::RenderGraphResultCode::InvalidExecutionState;
            std::snprintf(
                rejected.diagnostic.message.data(),
                rejected.diagnostic.message.size(),
                "%s",
                "RenderFrameGraph could not begin its owning session.");
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            return rejected;
        }
        GRAPH::RenderGraph* graph =
            INTERNAL::TryGetRenderFrameGraph(session);
        INTERNAL::StageExecutionState* state =
            INTERNAL::TryGetRenderFrameGraphStageState(session);
        if (graph == nullptr || state == nullptr) {
            (void)AbortRenderFrameGraphSession(session);
            rejected.diagnostic.code =
                GRAPH::RenderGraphResultCode::InvalidExecutionState;
            return rejected;
        }
        state->context = &context;
        state->session = session;
        state->presentationResumed = false;
        state->result.session = session;
        const auto rejectSession = [&]() noexcept {
            const RenderFrameGraphStageResult result = state->result;
            state->context = nullptr;
            (void)AbortRenderFrameGraphSession(session);
            return result;
        };

        const INTERNAL::RenderFrameGraphPlanOptions options{
            context.requiresSceneColorOpaque,
            context.hasSsrWork,
            context.hasDepthAwareWork,
            context.hasTransparentWork
        };
        if (!INTERNAL::BuildRenderFrameGraphPlan(
                options,
                state,
                INTERNAL::GetRenderFrameGraphPassCallbacks(),
                *graph)) {
            state->result.diagnostic = graph->GetDiagnostic();
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            return rejectSession();
        }
        if (!graph->Compile()) {
            state->result.diagnostic = graph->GetDiagnostic();
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            return rejectSession();
        }

        state->result.compiledPassCount = graph->GetCompiledPassCount();
        state->result.prefixCompleted = graph->ExecuteThrough(
            "RenderFeatures.Composite");
        state->result.pausedForPresentation =
            state->result.prefixCompleted &&
            graph->GetExecutionState().status ==
                GRAPH::RenderGraphExecutionStatus::Paused;
        state->result.prefixCompleted =
            state->result.prefixCompleted &&
            state->result.pausedForPresentation;
        state->result.diagnostic = graph->GetDiagnostic();
        if (!state->result.motionVectorsWritten) {
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
        }
        if (!state->result.prefixCompleted) {
            return rejectSession();
        }
        state->context = nullptr;
        static bool loggedGraphActivation = false;
        if (!loggedGraphActivation) {
            loggedGraphActivation = true;
            HIKARI_LOG_INFO(
                "[RenderFrameGraph] active resumable graph with " +
                std::to_string(state->result.compiledPassCount) +
                " pass(es).");
        }
        return state->result;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
