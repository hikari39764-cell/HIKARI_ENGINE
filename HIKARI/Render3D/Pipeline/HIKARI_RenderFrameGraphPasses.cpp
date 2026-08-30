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
                stage.sceneSurfaces.MatchesFrame(stage.frame.token);
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
            context.sceneSurfaces.packedSurface =
                context.screenResult.packedSurface;
            context.sceneSurfaces.surfaceFeatureMask =
                context.screenResult.surfaceFeatureMask;
            context.sceneSurfaces.ambientOcclusion =
                context.screenResult.ambientOcclusion;
            context.sceneSurfaces.currentSceneDepthPyramid =
                context.screenResult.currentSceneDepthPyramid;
            context.sceneSurfaces.visibilityDepthPyramid =
                context.screenResult.visibilityDepthPyramid;
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
            if (!TEMPORAL::TryBuildTemporalPassInputs(
                    context.temporalResources,
                    context.sceneSurfaces.sceneDepth,
                    context.sceneSurfaces.sceneColorOpaque,
                    temporalInputs)) {
                state.result.motionVectorsWritten = false;
                return false;
            }
            context.sceneSurfaces.historyColor =
                temporalInputs.historyColorRead;
            context.sceneSurfaces.historyDepth =
                temporalInputs.historyDepthRead;

            bool written = false;
            if (temporalInputs.motionVectors.valid &&
                context.sceneSurfaces.sceneDepth.IsSampleable() &&
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

            context.sceneSurfaces.motionVectors = written
                ? temporalInputs.motionVectors
                : RenderTextureView{};
            context.sceneSurfaces.motionMetadata = written
                ? temporalInputs.motionMetadata
                : RenderTextureView{};
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
            context.sceneSurfaces.motionVectors = {};
            context.sceneSurfaces.motionMetadata = {};
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
                POST::PostSystem::CaptureSceneColorSnapshot();
            if (!sceneColorOpaque.IsSampleable()) {
                return true;
            }
            context.sceneSurfaces.sceneColorOpaque = sceneColorOpaque;
            context.postOpaqueResources.opaqueSceneColor = sceneColorOpaque;
            (void)TEMPORAL::SetTemporalCompositionBase(
                context.temporalResources,
                sceneColorOpaque.srv);
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
