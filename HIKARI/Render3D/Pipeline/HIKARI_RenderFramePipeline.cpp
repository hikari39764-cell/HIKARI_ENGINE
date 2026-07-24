#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"

#include <algorithm>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"
#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {

    namespace {
        bool RebindPostRenderTarget(void*) {
            return POST::PostSystem::RebindCurrentRenderTarget();
        }

        bool BeginPostDepthRead(void*) {
            return POST::PostSystem::BeginCurrentRenderTargetDepthRead();
        }

        void EndPostDepthRead(void*) {
            POST::PostSystem::EndCurrentRenderTargetDepthRead();
        }

        RenderFrameContext BuildRenderFrameContext(
            const RenderViewContext& view,
            const SceneEnvironment& environment) {

            RenderFrameContext context{};
            context.viewId = view.viewId;
            context.purpose = view.purpose;
            context.cameraCut = view.cameraFrame->cameraCut;
            context.cmd = SERVICES::gCtx.cmdList;
            context.camera = &view.cameraFrame->camera;
            context.environment = &environment;
            context.scene.dsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            context.scene.readOnlyDsv =
                POST::PostSystem::GetCurrentRenderTargetReadOnlyDsv();
            context.scene.depthSrv =
                POST::PostSystem::GetCurrentRenderTargetDepthSrv();
            if (context.scene.depthSrv.ptr == 0) {
                context.scene.depthSrv = SERVICES::gCtx.sceneDepthSrv;
            }
            context.scene.depthReadable = context.scene.depthSrv.ptr != 0;
            context.targetAccess.rebind = RebindPostRenderTarget;
            context.targetAccess.beginDepthRead = BeginPostDepthRead;
            context.targetAccess.endDepthRead = EndPostDepthRead;

            int renderWidth = 0;
            int renderHeight = 0;
            POST::PostSystem::GetSceneCaptureSize(renderWidth, renderHeight);
            if (renderWidth <= 0 || renderHeight <= 0) {
                renderWidth = POST::PostSystem::GetSceneColorWidth();
                renderHeight = POST::PostSystem::GetSceneColorHeight();
            }
            context.renderWidth =
                static_cast<uint32_t>((std::max)(1, renderWidth));
            context.renderHeight =
                static_cast<uint32_t>((std::max)(1, renderHeight));

            int outputWidth = 0;
            int outputHeight = 0;
            POST::PostSystem::GetSceneOutputSize(outputWidth, outputHeight);
            context.outputWidth =
                static_cast<uint32_t>((std::max)(1, outputWidth));
            context.outputHeight =
                static_cast<uint32_t>((std::max)(1, outputHeight));
            return context;
        }

        ScreenSpacePassContext BuildScreenSpaceContext(
            const RenderFrameContext& frame) {

            ScreenSpacePassContext context{};
            context.cmd = frame.cmd;
            context.width = frame.renderWidth;
            context.height = frame.renderHeight;
            context.sceneDsv = frame.scene.dsv;
            context.readOnlySceneDsv = frame.scene.readOnlyDsv;
            context.sceneDepthSrv = frame.scene.depthSrv;
            context.depthReadable = frame.scene.depthReadable;
            context.renderTargetAccess = frame.targetAccess;
            return context;
        }

        MESHRENDERER::MeshPassResources BuildMeshPassResources(
            const ScreenSpacePassContext& context,
            const RENDER3D::SCREENSPACE::ScreenSpaceFrameResult& screenResult) {
            MESHRENDERER::MeshPassResources resources{};
            resources.sceneDepthSrv = context.sceneDepthSrv.ptr != 0
                ? context.sceneDepthSrv
                : SERVICES::gCtx.sceneDepthSrv;
            resources.ssaoSrv = screenResult.aoSrv;
            if (screenResult.depthPyramid.valid) {
                resources.depthPyramid = screenResult.depthPyramid;
            } else if (const RENDER3D::DEPTH::DepthPyramidView* depthPyramid =
                RENDER3D::DEPTH::TryGetFrameDepthPyramidView(
                    RENDER3D::DEPTH::DepthPyramidSourceKind::SceneDepth,
                    RENDER3D::DEPTH::DepthPyramidViewKind::CurrentFrame)) {
                resources.depthPyramid = *depthPyramid;
            }
            resources.fallbackAoTextureHandle = screenResult.fallbackAoTextureHandle;
            return resources;
        }

        bool ShouldRunSceneDepthPrepass(
            const RENDER3D::RenderQualitySettings& settings) {

            if (!settings.sceneDepthPrepass) {
                return false;
            }
            switch (settings.forwardCostMode) {
            case RENDER3D::ForwardShadingCostMode::AlbedoOnly:
            case RENDER3D::ForwardShadingCostMode::NoMaterialExtras:
                return false;
            default:
                return true;
            }
        }
    }

    bool RenderMeshLightingFrame(
        const RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return false;
        }

        const Camera3D& camera = view.cameraFrame->camera;
        RENDER3D::SetRenderDebugOutputView(debugView);
        const RENDER3D::RenderDebugOutputRoute& debugRoute =
            RENDER3D::GetRenderDebugOutputRoute();
        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "RenderFrame.MeshLighting");
        const RenderFrameContext frame =
            BuildRenderFrameContext(view, environment);
        const ScreenSpacePassContext screenSpaceContext =
            BuildScreenSpaceContext(frame);
        const RENDER3D::RenderQualitySettings& renderQuality =
            RENDER3D::GetRenderQualitySettings();
        const RENDER3D::RenderAntiAliasingMode antiAliasingMode =
            renderQuality.antiAliasingMode;
        static bool previousAntiAliasingModeValid = false;
        static RENDER3D::RenderAntiAliasingMode previousAntiAliasingMode =
            RENDER3D::RenderAntiAliasingMode::Off;
        static RENDER3D::DlssQualityMode previousDlssQualityMode =
            RENDER3D::DlssQualityMode::Quality;
        if (previousAntiAliasingModeValid &&
            (previousAntiAliasingMode != antiAliasingMode ||
                (antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS &&
                    previousDlssQualityMode != renderQuality.dlssQualityMode))) {
            RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
        }
        previousAntiAliasingMode = antiAliasingMode;
        previousDlssQualityMode = renderQuality.dlssQualityMode;
        previousAntiAliasingModeValid = true;

        RENDER3D::TEMPORAL::TemporalFrameDesc temporalDesc{};
        temporalDesc.camera = &camera;
        temporalDesc.frameIndex = TIME::GetFrameContext().frameIndex;
        temporalDesc.renderWidth = frame.renderWidth;
        temporalDesc.renderHeight = frame.renderHeight;
        temporalDesc.outputWidth = frame.outputWidth;
        temporalDesc.outputHeight = frame.outputHeight;
        temporalDesc.cameraCut = frame.cameraCut;
        const bool temporalDebugView = debugRoute.temporalVisualization;
        const bool volumetricDebugView = debugRoute.volumetricVisualization;
        const bool temporalPipelineAllowed =
            !debugRoute.active || temporalDebugView;
        temporalDesc.temporalResolveAllowed = temporalPipelineAllowed;
        temporalDesc.forceHistoryReset = !temporalPipelineAllowed;
        temporalDesc.jitterEnabled =
            RENDER3D::UsesTemporalJitter(antiAliasingMode) &&
            temporalPipelineAllowed;
        const RENDER3D::TEMPORAL::TemporalFrameState temporalFrame =
            RENDER3D::TEMPORAL::BeginTemporalFrame(temporalDesc);
        RENDER3D::TEMPORAL::UpdateTemporalResourceSystemContext(SERVICES::gCtx);
        (void)RENDER3D::TEMPORAL::BeginTemporalResources(temporalFrame);
        (void)RENDER3D::VOLUMETRIC::PrepareVolumetricLightingFrame(
            SERVICES::gCtx,
            temporalFrame,
            environment,
            renderQuality.volumetricLightingQuality,
            volumetricDebugView ? debugView : RenderDebugView::None);
        RENDER3D::TEMPORAL::SetTemporalDebugView(
            temporalDebugView ? debugView : RenderDebugView::None);
        const RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode =
            !debugRoute.bypassTemporalUpscaler
                ? RENDER3D::UPSCALING::ResolveStreamlineDlssMode(renderQuality)
                : RENDER3D::UPSCALING::StreamlineDlssMode::Off;
        (void)RENDER3D::UPSCALING::SubmitStreamlineFrameConstants(
            temporalFrame,
            streamlineMode);
        if (!POST::PostSystem::HasCurrentRenderTarget() ||
            !POST::PostSystem::RebindCurrentRenderTarget()) {
            return false;
        }
        MESHRENDERER::MeshFrameCameraOverrides cameraOverrides{};
        if (temporalFrame.camera.valid) {
            cameraOverrides.renderViewProj = &temporalFrame.camera.viewProj;
            cameraOverrides.renderInvViewProj = &temporalFrame.camera.invViewProj;
            cameraOverrides.cullingViewProj =
                &temporalFrame.camera.unjitteredViewProj;
            cameraOverrides.cullingInvViewProj =
                &temporalFrame.camera.invUnjitteredViewProj;
        }
        if (!MESHRENDERER::BeginFrame(
            camera,
            environment,
            screenSpaceContext.width,
            screenSpaceContext.height,
            (temporalDebugView || volumetricDebugView)
                ? RenderDebugView::None
                : debugView,
            temporalFrame.camera.valid ? &cameraOverrides : nullptr)) {
            MESHRENDERER::EndFrame();
            return false;
        }

        const MESHRENDERER::CameraCB* cameraCb = MESHRENDERER::GetCameraConstants();
        const MESHRENDERER::CameraCB* cullingCameraCb =
            MESHRENDERER::GetGpuDrivenCullingCameraConstants();

        RENDER3D::SCREENSPACE::ScreenSpaceFrameResult screenResult{};
        if (cameraCb != nullptr && cullingCameraCb != nullptr) {
            screenResult = RENDER3D::SCREENSPACE::ExecuteScreenSpacePreLightingPasses(
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(),
                screenSpaceContext,
                *cameraCb,
                *cullingCameraCb,
                environment);
        }
        else {
            RENDER3D::DEPTH::BeginDepthPyramidFrame(SERVICES::gCtx.frameIndex);
            RENDER3D::SCREENSPACE::EnsureScreenSpaceFallbacks(RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState());
            screenResult.fallbackAoTextureHandle =
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState().fallbackAoTextureHandle;
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();
        }

        // 不透明 mainline を先に scene DSV へ深度だけ描き、ForwardOpaque
        // (LESS_EQUAL) のピクセル過描画を early-Z で殺す。visibility 用の
        // occluder prepass (別ターゲット) とは独立している。
        const bool scenePrepassEnabled =
            ShouldRunSceneDepthPrepass(RENDER3D::GetRenderQualitySettings());
        const bool sceneDsvReady = screenSpaceContext.sceneDsv.ptr != 0;
        const bool depthPrepassWorkReady = MESHRENDERER::HasDepthPrepassWork();
        if (scenePrepassEnabled && sceneDsvReady && depthPrepassWorkReady) {
            bool prepassExecuted = false;
            {
                GFX::PIX::ScopedGpuEvent pixScenePrepass(
                    SERVICES::gCtx.cmdList,
                    GFX::PIX::kColorRender,
                    "SceneDepthPrepass");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuScenePrepass(
                    SERVICES::gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::DepthPrepass);
                prepassExecuted =
                    MESHRENDERER::RenderDepthPrepass(screenSpaceContext.sceneDsv);
                if (!POST::PostSystem::RebindCurrentRenderTarget()) {
                    MESHRENDERER::EndFrame();
                    return false;
                }
            }
            // 最初のフレームの状態だけを boot ヘルスチェックとして記録する。
            static bool sLoggedScenePrepassState = false;
            if (!sLoggedScenePrepassState) {
                sLoggedScenePrepassState = true;
                HIKARI_LOG_INFO(
                    std::string("[RenderFramePipeline] scene depth prepass: ") +
                    (prepassExecuted ? "executed" : "no gpu-driven work prepared"));
            }
            // Balanced SSAO は prepass が書いた当該フレームの深度から同フレーム
            // で作る (旧 temporal 経路の 1 フレーム遅れによる引き摺りを回避)。
            if (prepassExecuted && cameraCb != nullptr) {
                (void)RENDER3D::SCREENSPACE::ExecuteBalancedSsaoFromSceneDepth(
                    RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(),
                    screenSpaceContext,
                    *cameraCb,
                    environment,
                    screenResult);
            }
        } else if (scenePrepassEnabled) {
            static bool sWarnedScenePrepassSkip = false;
            if (!sWarnedScenePrepassSkip) {
                sWarnedScenePrepassSkip = true;
                HIKARI_LOG_WARN(
                    std::string("[RenderFramePipeline] scene depth prepass skipped:") +
                    (sceneDsvReady ? "" : " sceneDsv=null") +
                    (depthPrepassWorkReady ? "" : " depthPrepassSource=empty"));
            }
        }

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(screenResult.ssaoRendered);
        MESHRENDERER::MeshPassResources opaqueResources =
            BuildMeshPassResources(screenSpaceContext, screenResult);
        MESHRENDERER::MeshPassResources postOpaqueResources = opaqueResources;

        bool opaqueOk = false;
        {
            GFX::PIX::ScopedGpuEvent pixForward(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "ForwardOpaque");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuForward(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardOpaque);
            opaqueOk = MESHRENDERER::RenderForwardOpaquePass(
                opaqueResources);
        }
        if (opaqueOk && cameraCb != nullptr) {
            const bool postOpaqueOk =
                RENDER3D::SCREENSPACE::ExecuteScreenSpacePostOpaquePasses(
                    RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(),
                    screenSpaceContext,
                    *cameraCb,
                    environment,
                    screenResult);
            if (!postOpaqueOk) {
                MESHRENDERER::EndFrame();
                return false;
            }
            MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(screenResult.ssaoRendered);
            postOpaqueResources = BuildMeshPassResources(screenSpaceContext, screenResult);
        }
        if (opaqueOk) {
            const RENDER3D::TEMPORAL::TemporalInputs temporalInputs =
                RENDER3D::TEMPORAL::BuildTemporalInputs(
                    screenSpaceContext.sceneDepthSrv,
                    postOpaqueResources.sceneColorSrv);
            bool motionVectorsWritten = false;
            if (temporalInputs.motionVectors.valid &&
                screenSpaceContext.depthReadable &&
                screenSpaceContext.sceneDepthSrv.ptr != 0 &&
                screenSpaceContext.renderTargetAccess.BeginDepthRead()) {
                motionVectorsWritten =
                    RENDER3D::TEMPORAL::ExecuteMotionVectorPass(temporalInputs);
                if (motionVectorsWritten &&
                    screenSpaceContext.readOnlySceneDsv.ptr != 0) {
                    (void)RENDER3D::TEMPORAL::ExecuteTemporalGeometryPass(
                        temporalInputs,
                        screenSpaceContext.readOnlySceneDsv);
                }
                screenSpaceContext.renderTargetAccess.EndDepthRead();
                if (!screenSpaceContext.renderTargetAccess.Rebind()) {
                    MESHRENDERER::EndFrame();
                    return false;
                }
            }
            RENDER3D::TEMPORAL::MarkMotionVectorsWritten(motionVectorsWritten);
        } else {
            RENDER3D::TEMPORAL::MarkMotionVectorsWritten(false);
        }
        const bool hasDepthAwareWork = MESHRENDERER::HasDepthAwarePassWork();
        const bool hasTransparentWork = MESHRENDERER::HasForwardTransparentPassWork();
        bool depthAwareOk = true;
        if (opaqueOk && (hasDepthAwareWork || hasTransparentWork)) {
            if (POST::PostSystem::CaptureSceneColorSnapshot()) {
                postOpaqueResources.sceneColorSrv = POST::PostSystem::GetSceneColorSrv();
                RENDER3D::TEMPORAL::SetTemporalCompositionBase(
                    postOpaqueResources.sceneColorSrv);
                if (!POST::PostSystem::RebindCurrentRenderTarget()) {
                    MESHRENDERER::EndFrame();
                    return false;
                }
            }
        }
        if (opaqueOk && hasDepthAwareWork) {
            GFX::PIX::ScopedGpuEvent pixDepthAware(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "DepthAware");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuDepthAware(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::DepthAware);
            if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
                depthAwareOk = MESHRENDERER::RenderDepthAwarePass(
                    postOpaqueResources);
                POST::PostSystem::EndCurrentRenderTargetDepthRead();
            } else {
                depthAwareOk = false;
            }
        }

        bool transparentOk = true;
        if (opaqueOk && depthAwareOk && hasTransparentWork) {
            GFX::PIX::ScopedGpuEvent pixTransparent(SERVICES::gCtx.cmdList, GFX::PIX::kColorRender, "ForwardTransparent");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuTransparent(
                SERVICES::gCtx.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardTransparent);
            transparentOk = MESHRENDERER::RenderForwardTransparentPass(
                postOpaqueResources);
        }

        MESHRENDERER::EndFrame();
        return opaqueOk && depthAwareOk && transparentOk;
    }

    bool RenderMeshLightingFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        ResolvedCameraFrame cameraFrame{};
        cameraFrame.camera = camera;
        cameraFrame.valid = true;

        RenderViewContext view{};
        view.viewId = kPrimaryRenderViewId;
        view.purpose = RenderViewPurpose::Game;
        view.cameraFrame = &cameraFrame;
        return RenderMeshLightingFrame(view, environment, debugView);
    }

    bool RenderMeshCaptureOpaqueFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height) {

        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorRender,
            "RenderFrame.ReflectionProbeCaptureOpaque");

        if (!MESHRENDERER::BeginFrame(camera, environment, width, height)) {
            MESHRENDERER::EndFrame();
            return false;
        }
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(false);

        // Capture は後処理と depth-aware phase を含めない。
        const MESHRENDERER::MeshPassResources captureResources{};
        const bool opaqueOk = MESHRENDERER::RenderForwardOpaquePass(captureResources);
        MESHRENDERER::EndFrame();
        return opaqueOk;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
