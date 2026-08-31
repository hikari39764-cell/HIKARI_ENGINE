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
#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"
#include "Render3D/Features/HIKARI_StylizedScreenFeatureRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphStage.h"
#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE {

    namespace {
        FEATURES::StylizedScreenFeatureRenderer gStylizedScreenFeatures{};

        class TemporalFrameAbortGuard {
        public:
            TemporalFrameAbortGuard(
                TEMPORAL::TemporalViewLease view,
                RenderFrameToken frame) noexcept
                : view_(view), frame_(frame) {}

            ~TemporalFrameAbortGuard() noexcept {
                if (active_) {
                    try {
                        (void)TEMPORAL::AbortTemporalFrame(
                            view_,
                            frame_,
                            TEMPORAL::TemporalHistoryResetReason::
                                PreviousFrameIncomplete);
                    } catch (...) {
                        // The next Begin detects an unfinished publication and
                        // takes the cold-history path. Never throw from unwind.
                    }
                }
            }

            TemporalFrameAbortGuard(const TemporalFrameAbortGuard&) = delete;
            TemporalFrameAbortGuard& operator=(
                const TemporalFrameAbortGuard&) = delete;

            void HandOff() noexcept { active_ = false; }

        private:
            TEMPORAL::TemporalViewLease view_{};
            RenderFrameToken frame_{};
            bool active_ = true;
        };

        class PostSurfaceFrameAbortGuard {
        public:
            PostSurfaceFrameAbortGuard() noexcept = default;
            ~PostSurfaceFrameAbortGuard() noexcept {
                if (!active_) {
                    return;
                }
                try {
                    (void)POST::PostSystem::AbortSceneCaptureTemporalFrame(
                        TEMPORAL::TemporalHistoryResetReason::
                            PreviousFrameIncomplete);
                }
                catch (...) {
                    // Runtime shutdown retries the exact retained receipts.
                }
            }

            PostSurfaceFrameAbortGuard(
                const PostSurfaceFrameAbortGuard&) = delete;
            PostSurfaceFrameAbortGuard& operator=(
                const PostSurfaceFrameAbortGuard&) = delete;

            void HandOff() noexcept { active_ = false; }

        private:
            bool active_ = true;
        };

        bool RebindPostRenderTarget(void*) { return POST::PostSystem::RebindCurrentRenderTarget(); }
        bool BeginPostDepthRead(void*) { return POST::PostSystem::BeginCurrentRenderTargetDepthRead(); }
        void EndPostDepthRead(void*) { POST::PostSystem::EndCurrentRenderTargetDepthRead(); }
        bool ClearPostColor(
            void*,
            float red,
            float green,
            float blue,
            float alpha) noexcept {
            try {
                return POST::PostSystem::ClearCurrentRenderTargetColor(
                    red, green, blue, alpha);
            }
            catch (...) {
                return false;
            }
        }

        RenderFrameContext BuildRenderFrameContext(
            const GFX::Context& graphics,
            const RenderViewContext& view,
            const SceneEnvironment& environment) {

            RenderFrameContext context{};
            context.viewId = view.viewId;
            context.purpose = view.purpose;
            context.cameraCut = view.cameraFrame->cameraCut;
            context.projectionChanged =
                view.cameraFrame->projectionChanged;
            context.cameraSourceObjectId =
                view.cameraFrame->sourceCameraObjectId;
            context.cameraRevision = view.cameraFrame->revision;
            context.frameIndex = TIME::GetFrameContext().frameIndex;
            context.graphics = graphics;
            const SURFACE::RenderFrameSurfaceReceipt surfaceReceipt =
                POST::PostSystem::GetSceneCaptureSurfaceReceipt();
            if (surfaceReceipt.IsValid() &&
                surfaceReceipt.frame.MatchesFrameView(
                    context.frameIndex,
                    context.viewId) &&
                surfaceReceipt.deviceGeneration ==
                    context.graphics.deviceGeneration) {
                context.token = surfaceReceipt.frame;
                context.surfaceReceipt = surfaceReceipt;
            }
            context.camera = &view.cameraFrame->camera;
            context.environment = &environment;
            context.assetRegistry = view.assetRegistry;
            context.projectRenderExtensions = view.projectRenderExtensions;
            context.scene.dsv = POST::PostSystem::GetCurrentRenderTargetDsv();
            context.scene.readOnlyDsv =
                POST::PostSystem::GetCurrentRenderTargetReadOnlyDsv();
            context.targetAccess.rebind = RebindPostRenderTarget;
            context.targetAccess.beginDepthRead = BeginPostDepthRead;
            context.targetAccess.endDepthRead = EndPostDepthRead;
            context.targetAccess.clearColor = ClearPostColor;

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
            context.frame = frame.token;
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

        bool PublishOptionalTexture(
            SURFACE::SceneSurfaceSet& surfaces,
            GRAPH::RenderGraphSurfaceKey key,
            const RenderTextureView& texture,
            SURFACE::SceneSurfaceProducer producer,
            SURFACE::SceneSurfaceAvailability availability =
                SURFACE::SceneSurfaceAvailability::Produced) noexcept {
            return !texture.IsSampleable() ||
                surfaces.PublishTexture(
                    key,
                    texture,
                    producer,
                    availability);
        }

        bool PublishOptionalDepthPyramid(
            SURFACE::SceneSurfaceSet& surfaces,
            GRAPH::RenderGraphSurfaceKey key,
            const DEPTH::DepthPyramidView& pyramid) noexcept {
            return !pyramid.valid ||
                surfaces.PublishDepthPyramid(
                    key,
                    pyramid,
                    SURFACE::SceneSurfaceProducer::DepthPyramid);
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
        uint32_t projectPassCount = 0u;
        const bool hasProjectPassSink =
            view.projectRenderExtensions.CanExecutePasses();
        if (hasProjectPassSink &&
            !view.projectRenderExtensions.getPassCount(
                view.projectRenderExtensions.context,
                projectPassCount)) {
            return false;
        }
        const bool projectPassWork = projectPassCount != 0u;
        if (!MESHRENDERER::HasSubmittedItems() && !projectPassWork) {
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
        SURFACE::SceneSurfaceSet* sceneTextureRegistry =
            SURFACE::TryGetRenderFrameSurfaces(frame.surfaceReceipt);
        if (sceneTextureRegistry == nullptr ||
            !frame.surfaceReceipt.frame.Matches(frame.token)) {
            return false;
        }
        PostSurfaceFrameAbortGuard surfaceFrameGuard{};
        SURFACE::SceneSurfaceSet& sceneTextures = *sceneTextureRegistry;
        if (!sceneTextures.PublishTexture(
                GRAPH::RenderGraphSurfaceKey{
                    GRAPH::RenderGraphSemantic::SceneDepth,
                    GRAPH::RenderGraphSemanticPhase::Imported
                },
                frame.scene.depth,
                SURFACE::SceneSurfaceProducer::PostSceneDepth,
                SURFACE::SceneSurfaceAvailability::Imported)) {
            return false;
        }
        const RENDER3D::RenderQualitySettings& renderQuality =
            RENDER3D::GetRenderQualitySettings();
        const RENDER3D::RenderAntiAliasingMode antiAliasingMode =
            renderQuality.antiAliasingMode;

        RENDER3D::TEMPORAL::TemporalFrameDesc temporalDesc{};
        RENDER3D::TEMPORAL::TemporalViewLease temporalView{};
        if (!RENDER3D::TEMPORAL::AcquireTemporalView(
                frame.viewId,
                temporalView)) {
            return false;
        }
        temporalDesc.view = temporalView;
        temporalDesc.frame = frame.token;
        temporalDesc.purpose = frame.purpose;
        temporalDesc.camera = &camera;
        temporalDesc.cameraSourceObjectId = frame.cameraSourceObjectId;
        temporalDesc.cameraRevision = frame.cameraRevision;
        temporalDesc.renderWidth = frame.renderWidth;
        temporalDesc.renderHeight = frame.renderHeight;
        temporalDesc.outputWidth = frame.outputWidth;
        temporalDesc.outputHeight = frame.outputHeight;
        temporalDesc.cameraCut = frame.cameraCut;
        temporalDesc.projectionChanged = frame.projectionChanged;
        temporalDesc.historyConfigurationKey =
            static_cast<uint64_t>(antiAliasingMode) |
            (antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS
                ? static_cast<uint64_t>(renderQuality.dlssQualityMode) << 32u
                : 0u);
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
        if (!temporalFrame.view.Matches(temporalView) ||
            !temporalFrame.publication.Matches(frame.token) ||
            !temporalFrame.camera.valid) {
            return false;
        }
        TemporalFrameAbortGuard temporalFrameGuard(
            temporalView,
            frame.token);
        RENDER3D::TEMPORAL::TemporalResourceFrame temporalResources{};
        const bool temporalResourcesReady =
            RENDER3D::TEMPORAL::BeginTemporalResources(
                frame.graphics,
                temporalFrame,
                temporalResources);
        if (!temporalResourcesReady) {
            return false;
        }
        if (!POST::PostSystem::SetSceneCaptureTemporalFrame(
                temporalView,
                frame.token,
                temporalResources)) {
            (void)RENDER3D::TEMPORAL::AbortTemporalResources(
                temporalResources);
            return false;
        }
        temporalFrameGuard.HandOff();
        (void)RENDER3D::TEMPORAL::SetTemporalDebugView(
            temporalResources,
            temporalDebugView ? debugView : RenderDebugView::None);
        const RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode =
            !debugRoute.bypassTemporalUpscaler
                ? RENDER3D::UPSCALING::ResolveStreamlineDlssMode(renderQuality)
                : RENDER3D::UPSCALING::StreamlineDlssMode::Off;
        (void)RENDER3D::UPSCALING::SubmitStreamlineFrameConstants(
            temporalFrame,
            temporalResources,
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
        const bool hasDepthAwareWork =
            MESHRENDERER::HasDepthAwarePassWork();
        const bool hasTransparentWork =
            MESHRENDERER::HasForwardTransparentPassWork();
        const bool hasSsrWork =
            environment.screenSpaceReflections.enabled &&
            environment.screenSpaceReflections.quality !=
                ScreenSpaceReflectionQuality::Off;
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
            stylizedScreenMaskRequired || hasSsrWork;
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
        if (!PublishOptionalTexture(
                sceneTextures,
                GRAPH::RenderGraphSurfaceKey{
                    GRAPH::RenderGraphSemantic::PackedSurface,
                    GRAPH::RenderGraphSemanticPhase::PreOpaque
                },
                screenResult.packedSurface,
                SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
            (screenResult.ssaoRendered &&
             !PublishOptionalTexture(
                sceneTextures,
                GRAPH::RenderGraphSurfaceKey{
                    GRAPH::RenderGraphSemantic::AmbientOcclusion,
                    GRAPH::RenderGraphSemanticPhase::PreOpaque
                },
                screenResult.ambientOcclusion,
                SURFACE::SceneSurfaceProducer::Ssao)) ||
            !PublishOptionalDepthPyramid(
                sceneTextures,
                GRAPH::RenderGraphSurfaceKey{
                    GRAPH::RenderGraphSemantic::VisibilityDepthPyramid,
                    GRAPH::RenderGraphSemanticPhase::PreOpaque
                },
                screenResult.visibilityDepthPyramid)) {
            MESHRENDERER::EndFrame();
            return false;
        }
        MESHRENDERER::MeshPassResources opaqueResources =
            BuildMeshPassResources(
                sceneTextures,
                GRAPH::RenderGraphSemanticPhase::PreOpaque,
                screenResult.fallbackAoTextureHandle);
        MESHRENDERER::MeshPassResources postOpaqueResources = opaqueResources;
        FEATURES::RenderFeaturePlan renderFeaturePlan{};

        const RenderFrameGraphStageResult graphStage =
            ExecuteRenderFrameGraphStage(
                RenderFrameGraphStageContext{
                    frame,
                    screenSpaceContext,
                    temporalFrame,
                    temporalResources,
                    environment,
                    renderQuality,
                    RenderFrameGraphFeatureContext{
                        lighting,
                        featureRequests,
                        renderFeaturePlan,
                        gStylizedScreenFeatures,
                        !debugRoute.active,
                        geometryOutlineBackendReady,
                        visibilityOverlayBackendReady
                    },
                    cameraCb,
                    screenResult,
                    sceneTextures,
                    opaqueResources,
                    postOpaqueResources,
                    debugView,
                    hasDepthAwareWork,
                    hasTransparentWork,
                    hasSsrWork,
                    hasDepthAwareWork ||
                        hasTransparentWork ||
                        hasSsrWork ||
                        stylizedScreenMaskRequired
                });
        if (!graphStage.prefixCompleted ||
            !graphStage.pausedForPresentation ||
            !graphStage.session.IsValid()) {
            MESHRENDERER::EndFrame();
            return false;
        }
        if (!POST::PostSystem::SetSceneCaptureGraphSession(
                graphStage.session)) {
            (void)AbortRenderFrameGraphSession(graphStage.session);
            MESHRENDERER::EndFrame();
            return false;
        }
        MESHRENDERER::EndFrame();
        surfaceFrameGuard.HandOff();
        return true;
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

    void ShutdownRenderFramePipelineResources() noexcept {
        try {
            gStylizedScreenFeatures.Release();
        }
        catch (...) {
        }
        INTERNAL::ReleaseRenderFrameGraphPassResources();
    }

} // namespace HIKARI::RENDER3D::PIPELINE
