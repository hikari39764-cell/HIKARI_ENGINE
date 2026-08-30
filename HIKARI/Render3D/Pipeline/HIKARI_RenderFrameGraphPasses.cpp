#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_StylizedCloudStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Reflection/HIKARI_ScreenSpaceReflectionRenderer.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
    namespace {
        REFLECTION::ScreenSpaceReflectionRenderer gSsrRenderer{};

        bool ValidatePassContext(
            const GRAPH::RenderGraphPassContext& graphPass,
            const StageExecutionState& state) noexcept {

            if (state.context == nullptr) {
                return false;
            }
            const RenderFrameGraphStageContext& stage = *state.context;
            return graphPass.frame.Matches(stage.frame.token) &&
                stage.temporalFrame.publication.Matches(stage.frame.token) &&
                stage.temporalFrame.view.viewId == stage.frame.viewId &&
                stage.temporalResources.IsValid() &&
                stage.temporalResources.view.Matches(
                    stage.temporalFrame.view) &&
                stage.temporalResources.publication.Matches(
                    stage.frame.token) &&
                stage.sceneSurfaces.MatchesFrame(stage.frame.token) &&
                stage.sceneSurfaces.GetDeviceGeneration() ==
                    stage.frame.graphics.deviceGeneration;
        }

        bool ReplaceTexturePublication(
            SURFACE::SceneSurfaceSet& surfaces,
            GRAPH::RenderGraphSemantic semantic,
            const RenderTextureView& texture,
            SURFACE::SceneSurfaceProducer producer,
            SURFACE::SceneSurfaceAvailability availability =
                SURFACE::SceneSurfaceAvailability::Produced) noexcept {
            (void)surfaces.Invalidate(semantic);
            return !texture.IsSampleable() ||
                surfaces.PublishTexture(
                    semantic,
                    texture,
                    producer,
                    availability);
        }

        bool ReplaceDepthPyramidPublication(
            SURFACE::SceneSurfaceSet& surfaces,
            GRAPH::RenderGraphSemantic semantic,
            const DEPTH::DepthPyramidView& pyramid,
            SURFACE::SceneSurfaceProducer producer) noexcept {
            (void)surfaces.Invalidate(semantic);
            return !pyramid.valid ||
                surfaces.PublishDepthPyramid(
                    semantic,
                    pyramid,
                    producer);
        }
    }

    bool ExecuteForwardOpaque(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        try {
            GFX::PIX::ScopedGpuEvent pixForward(
                state.context->frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "ForwardOpaque");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuForward(
                state.context->frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardOpaque);
            state.result.opaqueSucceeded =
                MESHRENDERER::RenderForwardOpaquePass(
                    state.context->frame.graphics,
                    state.context->opaqueResources);
            return state.result.opaqueSucceeded;
        }
        catch (...) {
            state.result.opaqueSucceeded = false;
            return false;
        }
    }

    bool ExecutePostOpaqueSurfaces(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (context.cameraConstants == nullptr) {
            return true;
        }
        try {
            if (!SCREENSPACE::ExecuteScreenSpacePostOpaquePasses(
                    SCREENSPACE::GetScreenSpaceRuntimeState(),
                    context.screenSpace,
                    *context.cameraConstants,
                    context.environment,
                    context.screenResult)) {
                return false;
            }
            MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(
                context.screenResult.ssaoRendered);
            if (!ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::PackedSurface,
                    context.screenResult.packedSurface,
                    SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
                !ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
                    context.screenResult.surfaceFeatureMask,
                    SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
                !ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::StylizedOutlineStyle,
                    context.screenResult.stylizedOutlineStyle,
                    SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
                (context.screenResult.ssaoRendered
                    ? !ReplaceTexturePublication(
                        context.sceneSurfaces,
                        GRAPH::RenderGraphSemantic::AmbientOcclusion,
                        context.screenResult.ambientOcclusion,
                        SURFACE::SceneSurfaceProducer::Ssao)
                    : !context.sceneSurfaces.Invalidate(
                        GRAPH::RenderGraphSemantic::AmbientOcclusion)) ||
                !ReplaceDepthPyramidPublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::CurrentDepthPyramid,
                    context.screenResult.currentSceneDepthPyramid,
                    SURFACE::SceneSurfaceProducer::DepthPyramid) ||
                !ReplaceDepthPyramidPublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::VisibilityDepthPyramid,
                    context.screenResult.visibilityDepthPyramid,
                    SURFACE::SceneSurfaceProducer::DepthPyramid)) {
                return false;
            }
            context.postOpaqueResources = BuildMeshPassResources(
                context.sceneSurfaces,
                context.screenResult.fallbackAoTextureHandle);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteClouds(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        try {
            (void)CLOUDS::ExecuteStylizedCloudStage(
                context.frame,
                context.temporalFrame,
                context.environment.clouds,
                context.renderQuality.stylizedCloudQuality);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteMotionVectors(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        bool depthReadActive = false;
        try {
            TEMPORAL::TemporalInputs temporalInputs{};
            const RenderTextureView* sceneDepth =
                context.sceneSurfaces.TryGetTexture(
                    GRAPH::RenderGraphSemantic::SceneDepth);
            const RenderTextureView* sceneColorOpaque =
                context.sceneSurfaces.TryGetTexture(
                    GRAPH::RenderGraphSemantic::SceneColorOpaque);
            const RenderTextureView noSceneColor{};
            if (sceneDepth == nullptr) {
                return false;
            }
            if (!TEMPORAL::TryBuildTemporalPassInputs(
                    context.temporalResources,
                    *sceneDepth,
                    sceneColorOpaque != nullptr
                        ? *sceneColorOpaque
                        : noSceneColor,
                    temporalInputs)) {
                state.result.motionVectorsWritten = false;
                return false;
            }
            if (!ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::HistoryColor,
                    temporalInputs.historyColorRead,
                    SURFACE::SceneSurfaceProducer::TemporalHistory) ||
                !ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::HistoryDepth,
                    temporalInputs.historyDepthRead,
                    SURFACE::SceneSurfaceProducer::TemporalHistory)) {
                return false;
            }

            bool written = false;
            if (temporalInputs.motionVectors.valid &&
                sceneDepth->IsSampleable() &&
                context.screenSpace.renderTargetAccess.BeginDepthRead()) {
                depthReadActive = true;
                written = TEMPORAL::ExecuteMotionVectorPass(
                    context.frame.graphics,
                    temporalInputs);
                if (written) {
                    written =
                        context.screenSpace.readOnlySceneDsv.ptr != 0u &&
                        TEMPORAL::ExecuteTemporalGeometryPass(
                        context.frame.graphics,
                        temporalInputs,
                        context.screenSpace.readOnlySceneDsv);
                }
                context.screenSpace.renderTargetAccess.EndDepthRead();
                depthReadActive = false;
                if (!context.screenSpace.renderTargetAccess.Rebind()) {
                    return false;
                }
            }

            if (!ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::MotionVectors,
                    written
                        ? temporalInputs.motionVectors
                        : RenderTextureView{},
                    SURFACE::SceneSurfaceProducer::TemporalMotion) ||
                !ReplaceTexturePublication(
                    context.sceneSurfaces,
                    GRAPH::RenderGraphSemantic::MotionMetadata,
                    written
                        ? temporalInputs.motionMetadata
                        : RenderTextureView{},
                    SURFACE::SceneSurfaceProducer::TemporalMotion)) {
                return false;
            }
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                written);
            state.result.motionVectorsWritten = written;
            return written;
        }
        catch (...) {
            if (depthReadActive) {
                context.screenSpace.renderTargetAccess.EndDepthRead();
            }
            (void)context.sceneSurfaces.Invalidate(
                GRAPH::RenderGraphSemantic::MotionVectors);
            (void)context.sceneSurfaces.Invalidate(
                GRAPH::RenderGraphSemantic::MotionMetadata);
            (void)TEMPORAL::MarkMotionVectorsWritten(
                context.temporalResources,
                false);
            state.result.motionVectorsWritten = false;
            return false;
        }
    }

    bool ExecuteSceneColorOpaqueCapture(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        try {
            const RenderTextureView sceneColorOpaque =
                POST::PostSystem::CaptureSceneColorSnapshot(
                    POST::SceneColorSnapshotKind::Opaque);
            if (!sceneColorOpaque.IsSampleable()) {
                return false;
            }
            if (!context.sceneSurfaces.PublishTexture(
                    GRAPH::RenderGraphSemantic::SceneColorOpaque,
                    sceneColorOpaque,
                    SURFACE::SceneSurfaceProducer::SceneColorCapture)) {
                return false;
            }
            context.postOpaqueResources.opaqueSceneColor = sceneColorOpaque;
            if (!TEMPORAL::SetTemporalCompositionBase(
                    context.temporalResources,
                    sceneColorOpaque.srv)) {
                return false;
            }
            state.result.sceneColorOpaquePublished = true;
            return POST::PostSystem::RebindCurrentRenderTarget();
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteSsr(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidatePassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (context.cameraConstants == nullptr) {
            return true;
        }
        try {
            (void)gSsrRenderer.Render(
                context.frame.graphics,
                context.frame.targetAccess,
                context.sceneSurfaces,
                *context.cameraConstants,
                context.environment.screenSpaceReflections,
                context.debugView);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    const RenderFrameGraphPassCallbacks&
        GetRenderFrameGraphPassCallbacks() noexcept {
        static const RenderFrameGraphPassCallbacks callbacks{
            &ExecuteForwardOpaque,
            &ExecutePostOpaqueSurfaces,
            &ExecuteClouds,
            &ExecuteMotionVectors,
            &ExecuteSceneColorOpaqueCapture,
            &ExecuteSsr
        };
        return callbacks;
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
