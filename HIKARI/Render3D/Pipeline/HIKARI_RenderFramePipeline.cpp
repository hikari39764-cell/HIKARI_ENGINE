#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"
#include "Render3D/Features/HIKARI_RenderFeaturePlanner.h"
#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"
#include "Render3D/Features/HIKARI_StylizedScreenFeatureRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Reflection/HIKARI_ScreenSpaceReflectionRenderer.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {

    namespace {
        REFLECTION::ScreenSpaceReflectionRenderer gSsrRenderer{};
        FEATURES::StylizedScreenFeatureRenderer gStylizedScreenFeatures{};
        FEATURES::RenderFeaturePlan gLastRenderFeaturePlan{};

        bool RebindPostRenderTarget(void*) { return POST::PostSystem::RebindCurrentRenderTarget(); }
        bool BeginPostDepthRead(void*) { return POST::PostSystem::BeginCurrentRenderTargetDepthRead(); }
        void EndPostDepthRead(void*) { POST::PostSystem::EndCurrentRenderTargetDepthRead(); }

        RenderFrameContext BuildRenderFrameContext(
            const GFX::Context& graphics,
            const RenderViewContext& view,
            const SceneEnvironment& environment) {

            RenderFrameContext context{};
            context.viewId = view.viewId;
            context.purpose = view.purpose;
            context.cameraCut = view.cameraFrame->cameraCut;
            context.frameIndex = TIME::GetFrameContext().frameIndex;
            context.graphics = graphics;
            context.camera = &view.cameraFrame->camera;
            context.environment = &environment;
            context.scene.dsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            context.scene.readOnlyDsv =
                POST::PostSystem::GetCurrentRenderTargetReadOnlyDsv();
            context.targetAccess.rebind = RebindPostRenderTarget;
            context.targetAccess.beginDepthRead = BeginPostDepthRead;
            context.targetAccess.endDepthRead = EndPostDepthRead;

            int renderWidth = 0;
            int renderHeight = 0;
            POST::PostSystem::GetSceneCaptureSize(renderWidth, renderHeight);
            context.renderWidth =
                static_cast<uint32_t>((std::max)(1, renderWidth));
            context.renderHeight =
                static_cast<uint32_t>((std::max)(1, renderHeight));
            context.scene.depth =
                POST::PostSystem::GetCurrentRenderTargetDepthView();
            if (!context.scene.depth.IsSampleable() &&
                context.graphics.sceneDepthSrv.ptr != 0) {
                context.scene.depth.srv = context.graphics.sceneDepthSrv;
                context.scene.depth.format = DXGI_FORMAT_R32_FLOAT;
                context.scene.depth.width = context.renderWidth;
                context.scene.depth.height = context.renderHeight;
                context.scene.depth.mipCount = 1;
                context.scene.depth.state =
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                context.scene.depth.valid = true;
            }

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
            context.cmd = frame.graphics.cmdList;
            context.graphics = &frame.graphics;
            context.width = frame.renderWidth;
            context.height = frame.renderHeight;
            context.sceneDsv = frame.scene.dsv;
            context.readOnlySceneDsv = frame.scene.readOnlyDsv;
            context.sceneDepth = frame.scene.depth;
            context.renderTargetAccess = frame.targetAccess;
            return context;
        }

        MESHRENDERER::MeshPassResources BuildMeshPassResources(
            const SURFACE::SceneSurfaceSet& textures,
            int fallbackAoTextureHandle) {
            MESHRENDERER::MeshPassResources resources{};
            resources.sceneDepth = textures.sceneDepth;
            resources.opaqueSceneColor = textures.opaqueSceneColor;
            resources.ambientOcclusion = textures.ambientOcclusion;
            resources.depthPyramid = textures.currentSceneDepthPyramid.valid
                ? textures.currentSceneDepthPyramid
                : textures.visibilityDepthPyramid;
            resources.fallbackAoTextureHandle = fallbackAoTextureHandle;
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
        const GFX::Context& graphics,
        const RenderViewContext& view,
        const SceneEnvironment& environment,
        const LIGHTING::SceneLightingSet& lighting,
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

        const RenderFrameContext frame =
            BuildRenderFrameContext(graphics, view, environment);
        if (frame.graphics.cmdList == nullptr) {
            return false;
        }
        GFX::PIX::ScopedGpuEvent pixFrame(
            frame.graphics.cmdList,
            GFX::PIX::kColorRender,
            "RenderFrame.MeshLighting");
        const ScreenSpacePassContext screenSpaceContext =
            BuildScreenSpaceContext(frame);
        SURFACE::SceneSurfaceSet sceneTextures{};
        sceneTextures.frameIndex = frame.frameIndex;
        sceneTextures.viewId = frame.viewId;
        sceneTextures.sceneDepth = frame.scene.depth;
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
        RENDER3D::TEMPORAL::UpdateTemporalResourceSystemContext(frame.graphics);
        (void)RENDER3D::TEMPORAL::BeginTemporalResources(temporalFrame);
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
            frame.graphics,
            camera,
            environment,
            lighting,
            screenSpaceContext.width,
            screenSpaceContext.height,
            (temporalDebugView || volumetricDebugView)
                ? RenderDebugView::None
                : debugView,
            temporalFrame.camera.valid ? &cameraOverrides : nullptr)) {
            MESHRENDERER::EndFrame();
            return false;
        }
        const FEATURES::RenderFeatureRequestSet featureRequests =
            FEATURES::AnalyzeRenderFeatureRequests(
                MESHRENDERER::GetActiveGpuDrivenSceneSource());
        const bool stylizedFeatureMaskBackendReady =
            MESHRENDERER::HasStylizedFeatureMaskBackend();
        const bool geometryOutlineBackendReady =
            MESHRENDERER::HasGeometryOutlineFeatureBackend();
        const bool visibilityOverlayBackendReady =
            MESHRENDERER::HasVisibilityOverlayFeatureBackend();
        const bool stylizedScreenMaskRequired = !debugRoute.active &&
            stylizedFeatureMaskBackendReady &&
            (featureRequests.RequestsOpaqueSurface(
                 FEATURES::RenderFeatureKind::ScreenOutline) ||
             featureRequests.RequestsOpaqueSurface(
                 FEATURES::RenderFeatureKind::FaceHairShadow) ||
             (featureRequests.RequestsOpaqueSurface(
                  FEATURES::RenderFeatureKind::GeometryOutline) &&
              !geometryOutlineBackendReady));
        RENDER3D::SCREENSPACE::ScreenSpaceSurfaceRequirements
            screenSurfaceRequirements{};
        screenSurfaceRequirements.requirePackedSurface =
            stylizedScreenMaskRequired;
        screenSurfaceRequirements.requireFeatureMask =
            stylizedScreenMaskRequired;
        (void)RENDER3D::VOLUMETRIC::PrepareVolumetricLightingFrame(
            frame.graphics,
            temporalFrame,
            environment,
            MESHRENDERER::GetLocalLightListGpuBindings(),
            renderQuality.volumetricLightingQuality,
            volumetricDebugView ? debugView : RenderDebugView::None);

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
                environment,
                screenSurfaceRequirements);
        }
        else {
            RENDER3D::SCREENSPACE::EnsureScreenSpaceFallbacks(RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState());
            screenResult.fallbackAoTextureHandle =
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState().fallbackAoTextureHandle;
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(
                frame.graphics);
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
                    frame.graphics.cmdList,
                    GFX::PIX::kColorRender,
                    "SceneDepthPrepass");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuScenePrepass(
                    frame.graphics.cmdList,
                    GFX::GPU_PROFILE::Pass::DepthPrepass);
                prepassExecuted =
                    MESHRENDERER::RenderDepthPrepass(
                        frame.graphics,
                        screenSpaceContext.sceneDsv);
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
        sceneTextures.packedSurface = screenResult.packedSurface;
        sceneTextures.surfaceFeatureMask = screenResult.surfaceFeatureMask;
        sceneTextures.ambientOcclusion = screenResult.ambientOcclusion;
        sceneTextures.currentSceneDepthPyramid =
            screenResult.currentSceneDepthPyramid;
        sceneTextures.visibilityDepthPyramid =
            screenResult.visibilityDepthPyramid;
        MESHRENDERER::MeshPassResources opaqueResources =
            BuildMeshPassResources(
                sceneTextures,
                screenResult.fallbackAoTextureHandle);
        MESHRENDERER::MeshPassResources postOpaqueResources = opaqueResources;

        bool opaqueOk = false;
        {
            GFX::PIX::ScopedGpuEvent pixForward(
                frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "ForwardOpaque");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuForward(
                frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardOpaque);
            opaqueOk = MESHRENDERER::RenderForwardOpaquePass(
                frame.graphics,
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
            sceneTextures.packedSurface = screenResult.packedSurface;
            sceneTextures.surfaceFeatureMask = screenResult.surfaceFeatureMask;
            sceneTextures.ambientOcclusion = screenResult.ambientOcclusion;
            sceneTextures.currentSceneDepthPyramid =
                screenResult.currentSceneDepthPyramid;
            sceneTextures.visibilityDepthPyramid =
                screenResult.visibilityDepthPyramid;
            postOpaqueResources = BuildMeshPassResources(
                sceneTextures,
                screenResult.fallbackAoTextureHandle);
        }
        if (opaqueOk) {
            const RENDER3D::TEMPORAL::TemporalInputs temporalInputs =
                RENDER3D::TEMPORAL::BuildTemporalInputs(
                    sceneTextures.sceneDepth,
                    sceneTextures.opaqueSceneColor);
            sceneTextures.motionVectors = temporalInputs.motionVectors;
            sceneTextures.motionMetadata = temporalInputs.motionMetadata;
            sceneTextures.reactiveMask = temporalInputs.reactiveMask;
            sceneTextures.transparencyMask = temporalInputs.transparencyMask;
            sceneTextures.invalidDepthMotionMask =
                temporalInputs.invalidDepthMotionMask;
            sceneTextures.historyColor = temporalInputs.historyColorRead;
            sceneTextures.historyDepth = temporalInputs.historyDepthRead;
            bool motionVectorsWritten = false;
            if (temporalInputs.motionVectors.valid &&
                sceneTextures.sceneDepth.IsSampleable() &&
                screenSpaceContext.renderTargetAccess.BeginDepthRead()) {
                motionVectorsWritten =
                    RENDER3D::TEMPORAL::ExecuteMotionVectorPass(
                        frame.graphics,
                        temporalInputs);
                if (motionVectorsWritten &&
                    screenSpaceContext.readOnlySceneDsv.ptr != 0) {
                    (void)RENDER3D::TEMPORAL::ExecuteTemporalGeometryPass(
                        frame.graphics,
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
        const bool hasSsrWork =
            environment.screenSpaceReflections.enabled &&
            environment.screenSpaceReflections.quality !=
                ScreenSpaceReflectionQuality::Off;
        bool depthAwareOk = true;
        if (opaqueOk &&
            (hasDepthAwareWork || hasTransparentWork || hasSsrWork)) {
            const RenderTextureView opaqueSceneColor =
                POST::PostSystem::CaptureSceneColorSnapshot();
            if (opaqueSceneColor.IsSampleable()) {
                sceneTextures.opaqueSceneColor = opaqueSceneColor;
                postOpaqueResources.opaqueSceneColor = opaqueSceneColor;
                RENDER3D::TEMPORAL::SetTemporalCompositionBase(
                    opaqueSceneColor.srv);
                if (!POST::PostSystem::RebindCurrentRenderTarget()) {
                    MESHRENDERER::EndFrame();
                    return false;
                }
            }
        }
        if (opaqueOk && hasSsrWork && cameraCb != nullptr) {
            (void)gSsrRenderer.Render(
                frame.graphics,
                frame.targetAccess,
                sceneTextures,
                *cameraCb,
                environment.screenSpaceReflections,
                debugView);
        }
        if (opaqueOk && hasDepthAwareWork) {
            GFX::PIX::ScopedGpuEvent pixDepthAware(
                frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "DepthAware");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuDepthAware(
                frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::DepthAware);
            if (POST::PostSystem::BeginCurrentRenderTargetDepthRead()) {
                depthAwareOk = MESHRENDERER::RenderDepthAwarePass(
                    frame.graphics,
                    postOpaqueResources);
                POST::PostSystem::EndCurrentRenderTargetDepthRead();
            } else {
                depthAwareOk = false;
            }
        }

        bool transparentOk = true;
        if (opaqueOk && depthAwareOk && hasTransparentWork) {
            GFX::PIX::ScopedGpuEvent pixTransparent(
                frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "ForwardTransparent");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuTransparent(
                frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardTransparent);
            transparentOk = MESHRENDERER::RenderForwardTransparentPass(
                frame.graphics,
                postOpaqueResources);
        }

        FEATURES::RenderFeatureAvailability featureAvailability{};
        featureAvailability.width = frame.renderWidth;
        featureAvailability.height = frame.renderHeight;
        featureAvailability.enabledMask = debugRoute.active ? 0u : 0xffffffffu;
        featureAvailability.inputMask = 0u;
        if (geometryOutlineBackendReady) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::GeometryBackend);
        }
        if (visibilityOverlayBackendReady) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::VisibilityBackend);
        }
        if (frame.targetAccess.rebind != nullptr) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneColor);
        }
        if (sceneTextures.sceneDepth.IsSampleable()) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneDepth);
        }
        if (sceneTextures.packedSurface.IsSampleable()) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceNormals);
        }
        if (sceneTextures.surfaceFeatureMask.IsSampleable()) {
            featureAvailability.inputMask |=
                FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceFeatureMask);
        }
        gLastRenderFeaturePlan = FEATURES::BuildRenderFeaturePlan(
            featureRequests,
            featureAvailability);
        bool featurePassesOk = true;
        const auto& geometryFeature = gLastRenderFeaturePlan.entries[
            static_cast<size_t>(FEATURES::RenderFeatureKind::GeometryOutline)];
        if (opaqueOk && depthAwareOk && transparentOk &&
            geometryFeature.scheduled &&
            !geometryFeature.compatibilityFallback) {
            featurePassesOk = frame.targetAccess.Rebind() &&
                MESHRENDERER::RenderGeometryOutlineFeaturePass(
                    frame.graphics,
                    postOpaqueResources);
        }
        const auto& visibilityFeature = gLastRenderFeaturePlan.entries[
            static_cast<size_t>(FEATURES::RenderFeatureKind::VisibilityOverlay)];
        if (featurePassesOk && opaqueOk && depthAwareOk && transparentOk &&
            visibilityFeature.scheduled) {
            featurePassesOk = frame.targetAccess.Rebind() &&
                MESHRENDERER::RenderVisibilityOverlayFeaturePass(
                    frame.graphics,
                    postOpaqueResources);
        }
        if (opaqueOk && depthAwareOk && transparentOk &&
            featurePassesOk &&
            gLastRenderFeaturePlan.screenCompositePassRequired) {
            const RenderTextureView finalSceneColor =
                POST::PostSystem::CaptureSceneColorSnapshot();
            if (finalSceneColor.IsSampleable()) {
                sceneTextures.opaqueSceneColor = finalSceneColor;
                FEATURES::StylizedScreenFeatureSettings featureSettings{};
                if (cameraCb != nullptr) {
                    featureSettings.viewProj = cameraCb->viewProj;
                }
                if (const LIGHTING::SceneLightProvider* directional =
                        LIGHTING::SelectDirectionalLight(lighting)) {
                    featureSettings.directionalLightWS[0] =
                        directional->direction.x;
                    featureSettings.directionalLightWS[1] =
                        directional->direction.y;
                    featureSettings.directionalLightWS[2] =
                        directional->direction.z;
                } else {
                    featureSettings.directionalLightWS[0] =
                        environment.directional.direction.x;
                    featureSettings.directionalLightWS[1] =
                        environment.directional.direction.y;
                    featureSettings.directionalLightWS[2] =
                        environment.directional.direction.z;
                }
                featurePassesOk = gStylizedScreenFeatures.Render(
                    frame.graphics,
                    frame.targetAccess,
                    sceneTextures,
                    gLastRenderFeaturePlan,
                    featureSettings);
            }
        }

        MESHRENDERER::EndFrame();
        return opaqueOk && depthAwareOk && transparentOk && featurePassesOk;
    }

    bool RenderMeshCaptureOpaqueFrame(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const LIGHTING::SceneLightingSet& lighting,
        uint32_t width,
        uint32_t height) {

        if (!MESHRENDERER::HasSubmittedItems()) {
            return true;
        }

        GFX::PIX::ScopedGpuEvent pixFrame(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "RenderFrame.ReflectionProbeCaptureOpaque");

        if (!MESHRENDERER::BeginFrame(
                graphics,
                camera,
                environment,
                lighting,
                width,
                height)) {
            MESHRENDERER::EndFrame();
            return false;
        }
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(graphics);

        MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(false);

        // Capture は後処理と depth-aware phase を含めない。
        const MESHRENDERER::MeshPassResources captureResources{};
        const bool opaqueOk =
            MESHRENDERER::RenderForwardOpaquePass(graphics, captureResources);
        MESHRENDERER::EndFrame();
        return opaqueOk;
    }

    const FEATURES::RenderFeaturePlan& GetLastRenderFeaturePlan() {
        return gLastRenderFeaturePlan;
    }

} // namespace HIKARI::RENDER3D::PIPELINE
