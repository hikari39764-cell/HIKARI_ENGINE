#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <string>
#include <optional>

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
#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Pipeline/HIKARI_SunGlareFrameBuilder.h"
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
RAYTRACING::RuntimeRayTracingScene gRuntimeRayScene;
RAYTRACING::RuntimeRayTracingLighting gRuntimeRayLighting;
DYNAMICGI::DynamicDiffuseGi gDynamicDiffuseGi;
uint64_t gPrimaryFrameFailureCount = 0;

void LogPrimaryPipelineFailure(uint64_t frameIndex, const char* stage, const char* detail = nullptr) {
    ++gPrimaryFrameFailureCount;
    static uint64_t lastReportedFrame = 0u;
    if (lastReportedFrame != 0u && frameIndex - lastReportedFrame < 120u) {
        return;
    }
    lastReportedFrame = frameIndex;
    std::string message = "[RenderFramePipeline] primary frame rejected at ";
    message += stage != nullptr ? stage : "unknown stage";
    if (detail != nullptr && detail[0] != '\0') {
        message += ": ";
        message += detail;
    }
    HIKARI_LOG_ERROR(message);
}

class TemporalFrameAbortGuard {
public:
    TemporalFrameAbortGuard(TEMPORAL::TemporalViewLease view, RenderFrameToken frame) noexcept
        : view_(view), frame_(frame) {}

    ~TemporalFrameAbortGuard() noexcept {
        if (active_) {
            try {
                (void)TEMPORAL::AbortTemporalFrame(view_, frame_,
                                                   TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
            } catch (...) {
                // The next Begin detects an unfinished publication and
                // takes the cold-history path. Never throw from unwind.
            }
        }
    }

    TemporalFrameAbortGuard(const TemporalFrameAbortGuard&) = delete;
    TemporalFrameAbortGuard& operator=(const TemporalFrameAbortGuard&) = delete;

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
                TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        } catch (...) {
            // Runtime shutdown retries the exact retained receipts.
        }
    }

    PostSurfaceFrameAbortGuard(const PostSurfaceFrameAbortGuard&) = delete;
    PostSurfaceFrameAbortGuard& operator=(const PostSurfaceFrameAbortGuard&) = delete;

    void HandOff() noexcept { active_ = false; }

private:
    bool active_ = true;
};

bool RebindPostRenderTarget(void*) { return POST::PostSystem::RebindCurrentRenderTarget(); }
bool BindPostAdditionalColors(void*, std::span<const D3D12_CPU_DESCRIPTOR_HANDLE> colors) {
    return POST::PostSystem::BindAdditionalColors(colors);
}
bool BeginPostDepthRead(void*) { return POST::PostSystem::BeginCurrentRenderTargetDepthRead(); }
void EndPostDepthRead(void*) { POST::PostSystem::EndCurrentRenderTargetDepthRead(); }
bool ClearPostColor(void*, float red, float green, float blue, float alpha) noexcept {
    try {
        return POST::PostSystem::ClearCurrentRenderTargetColor(red, green, blue, alpha);
    } catch (...) {
        return false;
    }
}
bool CopyPostColorFrom(void*, ID3D12Resource* source, DXGI_FORMAT format, uint32_t width, uint32_t height) noexcept {
    try {
        return POST::PostSystem::CopyToCurrentRenderTargetColor(source, format, width, height);
    } catch (...) {
        return false;
    }
}

RenderFrameContext BuildRenderFrameContext(const GFX::Context& graphics, const RenderViewContext& view,
                                           const SceneEnvironment& environment) {
    RenderFrameContext context{};
    context.viewId = view.viewId;
    context.purpose = view.purpose;
    context.cameraCut = view.cameraFrame->cameraCut;
    context.projectionChanged = view.cameraFrame->projectionChanged;
    context.cameraSourceObjectId = view.cameraFrame->sourceCameraObjectId;
    context.cameraRevision = view.cameraFrame->revision;
    context.frameIndex = TIME::GetFrameContext().frameIndex;
    context.graphics = graphics;
    const SURFACE::RenderFrameSurfaceReceipt surfaceReceipt = POST::PostSystem::GetSceneCaptureSurfaceReceipt();
    if (surfaceReceipt.IsValid() && surfaceReceipt.frame.MatchesFrameView(context.frameIndex, context.viewId) &&
        surfaceReceipt.deviceGeneration == context.graphics.deviceGeneration) {
        context.token = surfaceReceipt.frame;
        context.surfaceReceipt = surfaceReceipt;
    }
    context.camera = &view.cameraFrame->camera;
    context.screenReadability = ProjectScreenReadability(view.screenReadability, context.camera->GetViewProj());
    context.environment = &environment;
    context.assetRegistry = view.assetRegistry;
    context.projectRenderExtensions = view.projectRenderExtensions;
    context.scene.dsv = POST::PostSystem::GetCurrentRenderTargetDsv();
    context.scene.readOnlyDsv = POST::PostSystem::GetCurrentRenderTargetReadOnlyDsv();
    context.targetAccess.rebind = RebindPostRenderTarget;
    context.targetAccess.bindAdditionalColors = BindPostAdditionalColors;
    context.targetAccess.beginDepthRead = BeginPostDepthRead;
    context.targetAccess.endDepthRead = EndPostDepthRead;
    context.targetAccess.clearColor = ClearPostColor;
    context.targetAccess.copyColorFrom = CopyPostColorFrom;

    int renderWidth = 0;
    int renderHeight = 0;
    POST::PostSystem::GetSceneCaptureSize(renderWidth, renderHeight);
    context.renderWidth = static_cast<uint32_t>((std::max)(1, renderWidth));
    context.renderHeight = static_cast<uint32_t>((std::max)(1, renderHeight));
    context.scene.depth = POST::PostSystem::GetCurrentRenderTargetDepthView();
    context.scene.color = POST::PostSystem::GetCurrentRenderTargetColorView();
    if (!context.scene.depth.IsSampleable() && context.graphics.sceneDepthSrv.ptr != 0) {
        context.scene.depth.srv = context.graphics.sceneDepthSrv;
        context.scene.depth.format = DXGI_FORMAT_R32_FLOAT;
        context.scene.depth.width = context.renderWidth;
        context.scene.depth.height = context.renderHeight;
        context.scene.depth.mipCount = 1;
        context.scene.depth.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        context.scene.depth.valid = true;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    POST::PostSystem::GetSceneOutputSize(outputWidth, outputHeight);
    context.outputWidth = static_cast<uint32_t>((std::max)(1, outputWidth));
    context.outputHeight = static_cast<uint32_t>((std::max)(1, outputHeight));
    return context;
}

ScreenSpacePassContext BuildScreenSpaceContext(const RenderFrameContext& frame) {
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

bool ShouldRunSceneDepthPrepass(const RENDER3D::RenderQualitySettings& settings) {
    if (!settings.sceneDepthPrepass) {
        return false;
    }
    return settings.forwardCostMode != RENDER3D::ForwardShadingCostMode::AlbedoOnly;
}

bool PublishOptionalTexture(
    SURFACE::SceneSurfaceSet& surfaces, GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
    SURFACE::SceneSurfaceProducer producer,
    SURFACE::SceneSurfaceAvailability availability = SURFACE::SceneSurfaceAvailability::Produced) noexcept {
    return !texture.IsSampleable() || surfaces.PublishTexture(key, texture, producer, availability);
}

bool PublishOptionalDepthPyramid(SURFACE::SceneSurfaceSet& surfaces, GRAPH::RenderGraphSurfaceKey key,
                                 const DEPTH::DepthPyramidView& pyramid) noexcept {
    return !pyramid.valid || surfaces.PublishDepthPyramid(key, pyramid, SURFACE::SceneSurfaceProducer::DepthPyramid);
}
} // namespace

bool RenderMeshLightingFrame(const GFX::Context& graphics, const RenderViewContext& view,
                             const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                             RenderDebugView debugView) {
    if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
        return false;
    }

    const Camera3D& camera = view.cameraFrame->camera;
    POST::PostSystem::SetSunGlareFrame(BuildSunGlareFrame(camera, environment));
    RENDER3D::SetRenderDebugOutputView(debugView);
    const RENDER3D::RenderDebugOutputRoute& debugRoute = RENDER3D::GetRenderDebugOutputRoute();
    POST::PostSystem::SetActionPostView(view.projectRenderExtensions.worldIdentity,
                                        view.viewId == kPrimaryRenderViewId &&
                                            view.purpose == RenderViewPurpose::Game && !debugRoute.active);
    uint32_t projectPassCount = 0u;
    const bool hasProjectPassSink = view.projectRenderExtensions.CanExecutePasses();
    if (hasProjectPassSink &&
        !view.projectRenderExtensions.getPassCount(view.projectRenderExtensions.context, projectPassCount)) {
        return false;
    }
    const bool projectPassWork = projectPassCount != 0u;
    if (!MESHRENDERER::HasSubmittedItems() && !projectPassWork) {
        return true;
    }

    const RenderFrameContext frame = BuildRenderFrameContext(graphics, view, environment);
    POST::PostSystem::SetScreenReadability(frame.screenReadability);
    if (frame.graphics.cmdList == nullptr) {
        return false;
    }
    GFX::PIX::ScopedGpuEvent pixFrame(frame.graphics.cmdList, GFX::PIX::kColorRender, "RenderFrame.MeshLighting");
    ScreenSpacePassContext screenSpaceContext = BuildScreenSpaceContext(frame);
    SURFACE::SceneSurfaceSet* sceneTextureRegistry = SURFACE::TryGetRenderFrameSurfaces(frame.surfaceReceipt);
    if (sceneTextureRegistry == nullptr || !frame.surfaceReceipt.frame.Matches(frame.token)) {
        return false;
    }
    PostSurfaceFrameAbortGuard surfaceFrameGuard{};
    SURFACE::SceneSurfaceSet& sceneTextures = *sceneTextureRegistry;
    if (!sceneTextures.PublishTexture(GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::SceneDepth,
                                                                   GRAPH::RenderGraphSemanticPhase::Imported},
                                      frame.scene.depth, SURFACE::SceneSurfaceProducer::PostSceneDepth,
                                      SURFACE::SceneSurfaceAvailability::Imported)) {
        return false;
    }
    const RENDER3D::RenderQualitySettings& renderQuality = RENDER3D::GetRenderQualitySettings();
    const RENDER3D::RenderAntiAliasingMode antiAliasingMode = renderQuality.antiAliasingMode;

    RENDER3D::TEMPORAL::TemporalFrameDesc temporalDesc{};
    RENDER3D::TEMPORAL::TemporalViewLease temporalView{};
    if (!RENDER3D::TEMPORAL::AcquireTemporalView(frame.viewId, temporalView)) {
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
    const uint64_t depthOfFieldEnabledBit = camera.GetDepthOfFieldSettings().enabled ? uint64_t{1} << 48u : 0u;
    const uint64_t depthOfFieldQualityBits = static_cast<uint64_t>(renderQuality.depthOfFieldQuality) << 49u;
    temporalDesc.historyConfigurationKey = static_cast<uint64_t>(antiAliasingMode) |
                                           (antiAliasingMode == RENDER3D::RenderAntiAliasingMode::DLSS
                                                ? static_cast<uint64_t>(renderQuality.dlssQualityMode) << 32u
                                                : 0u) |
                                           depthOfFieldEnabledBit | depthOfFieldQualityBits;
    const bool temporalDebugView = debugRoute.temporalVisualization;
    const bool volumetricDebugView = debugRoute.volumetricVisualization;
    const bool temporalPipelineAllowed = !debugRoute.active || temporalDebugView;
    temporalDesc.temporalResolveAllowed = temporalPipelineAllowed;
    temporalDesc.forceHistoryReset = !temporalPipelineAllowed;
    temporalDesc.jitterEnabled = RENDER3D::UsesTemporalJitter(antiAliasingMode) && temporalPipelineAllowed;
    const RENDER3D::TEMPORAL::TemporalFrameState temporalFrame = RENDER3D::TEMPORAL::BeginTemporalFrame(temporalDesc);
    if (!temporalFrame.view.Matches(temporalView) || !temporalFrame.publication.Matches(frame.token) ||
        !temporalFrame.camera.valid) {
        return false;
    }
    TemporalFrameAbortGuard temporalFrameGuard(temporalView, frame.token);
    RENDER3D::TEMPORAL::TemporalResourceFrame temporalResources{};
    const bool temporalResourcesReady =
        RENDER3D::TEMPORAL::BeginTemporalResources(frame.graphics, temporalFrame, temporalResources);
    if (!temporalResourcesReady) {
        LogPrimaryPipelineFailure(frame.frameIndex, "temporal resource begin");
        return false;
    }
    if (!POST::PostSystem::SetSceneCaptureTemporalFrame(temporalView, frame.token, temporalResources)) {
        (void)RENDER3D::TEMPORAL::AbortTemporalResources(temporalResources);
        return false;
    }
    temporalFrameGuard.HandOff();
    (void)RENDER3D::TEMPORAL::SetTemporalDebugView(temporalResources,
                                                   temporalDebugView ? debugView : RenderDebugView::None);
    const RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode =
        !debugRoute.bypassTemporalUpscaler ? RENDER3D::UPSCALING::ResolveStreamlineDlssMode(renderQuality)
                                           : RENDER3D::UPSCALING::StreamlineDlssMode::Off;
    (void)RENDER3D::UPSCALING::SubmitStreamlineFrameConstants(temporalFrame, temporalResources, streamlineMode);
    if (!POST::PostSystem::HasCurrentRenderTarget() || !POST::PostSystem::RebindCurrentRenderTarget()) {
        return false;
    }
    MESHRENDERER::MeshFrameCameraOverrides cameraOverrides{};
    const auto cloudSolarVisibility =
        CLOUDS::PrepareCloudLightingFrame(frame, temporalFrame, environment, renderQuality.volumetricCloudQuality);
    if (temporalFrame.camera.valid) {
        cameraOverrides.renderViewProj = &temporalFrame.camera.viewProj;
        cameraOverrides.renderInvViewProj = &temporalFrame.camera.invViewProj;
        cameraOverrides.cullingViewProj = &temporalFrame.camera.unjitteredViewProj;
        cameraOverrides.cullingInvViewProj = &temporalFrame.camera.invUnjitteredViewProj;
    }
    if (!MESHRENDERER::BeginFrame(frame.graphics, camera, environment, lighting, screenSpaceContext.width,
                                  screenSpaceContext.height,
                                  (temporalDebugView || volumetricDebugView) ? RenderDebugView::None : debugView,
                                  temporalFrame.camera.valid ? &cameraOverrides : nullptr)) {
        MESHRENDERER::EndFrame();
        return false;
    }
    DYNAMICGI::Frame dynamicGi;
    RAYTRACING::RuntimeRayLightingFrame rayLighting;
    MESHRENDERER::MeshMaterialGpuFrameView materials{};
    std::optional<LIGHTPROBE::ScopedDynamicGiVolumes> dynamicGiScope;
    {
        const auto& source = MESHRENDERER::GetActiveGpuDrivenSceneSource();
        const auto& clock = TIME::GetFrameContext();
        const auto giQuality = renderQuality.lightProbeVolumeSampling == LightProbeVolumeSamplingMode::Off
                                   ? DynamicGiQuality::Off
                                   : renderQuality.dynamicGiQuality;
        // 光線追跡の反射も同じ加速構造を借りる。
        const bool rayReflections = environment.screenSpaceReflections.enabled &&
                                    environment.screenSpaceReflections.quality != ScreenSpaceReflectionQuality::Off &&
                                    environment.screenSpaceReflections.rayTracingFallback;
        materials = MESHRENDERER::GetMaterialGpuFrameView(frame.graphics);
        RAYTRACING::RuntimeRayTracingFrame rayScene;
        if ((giQuality != DynamicGiQuality::Off || rayReflections) && materials) {
            GFX::GPU_PROFILE::ScopedGpuTimer rayTimer(frame.graphics.cmdList,
                                                      GFX::GPU_PROFILE::Pass::RuntimeRayScenePreparation);
            rayScene = gRuntimeRayScene.Prepare(frame.graphics, source, materials.materialSlots, materials.materialData,
                                                {}, {materials.dataVersion, materials.bindingVersion});
        } else {
            gRuntimeRayScene.Reset(frame.graphics);
            gRuntimeRayLighting.Reset(frame.graphics);
        }
        rayLighting = gRuntimeRayLighting.Prepare(
            frame.graphics, rayScene, materials, environment, lighting,
            {TIME::GetUnscaledElapsedSeconds(), clock.unscaledDt, clock.gameDt, static_cast<float>(clock.frameIndex)},
            cloudSolarVisibility.data);
        dynamicGi =
            gDynamicDiffuseGi.Prepare(frame.graphics, rayScene, materials, rayLighting, lighting.irradianceVolumes,
                                      camera.GetPosition(), camera.GetTarget(), giQuality, frame.frameIndex);
        if (dynamicGi.count != 0)
            dynamicGiScope.emplace(dynamicGi.Volumes());
        if (!MESHRENDERER::RefreshLightingProviders(frame.graphics, cloudSolarVisibility.data)) {
            MESHRENDERER::EndFrame();
            return false;
        }
    }
    const FEATURES::RenderFeatureRequestSet featureRequests =
        FEATURES::AnalyzeRenderFeatureRequests(MESHRENDERER::GetActiveGpuDrivenSceneSource());
    const bool hasDepthAwareWork = MESHRENDERER::HasDepthAwarePassWork();
    const bool hasTransparentWork = MESHRENDERER::HasForwardTransparentPassWork();
    const bool hasSsrWork = environment.screenSpaceReflections.enabled &&
                            environment.screenSpaceReflections.quality != ScreenSpaceReflectionQuality::Off;
    const bool hasSubsurfaceWork =
        environment.subsurfaceScattering.enabled && environment.subsurfaceScattering.strength > 0.0f;
    const bool stylizedFeatureMaskBackendReady = MESHRENDERER::HasStylizedFeatureMaskBackend();
    const bool geometryOutlineBackendReady = MESHRENDERER::HasGeometryOutlineFeatureBackend();
    const bool visibilityOverlayBackendReady = MESHRENDERER::HasVisibilityOverlayFeatureBackend();
    const bool stylizedScreenMaskRequired =
        !debugRoute.active && stylizedFeatureMaskBackendReady &&
        (featureRequests.RequestsOpaqueSurface(FEATURES::RenderFeatureKind::ScreenOutline) ||
         featureRequests.RequestsOpaqueSurface(FEATURES::RenderFeatureKind::FaceHairShadow) ||
         (featureRequests.RequestsOpaqueSurface(FEATURES::RenderFeatureKind::GeometryOutline) &&
          !geometryOutlineBackendReady));
    const bool globalPostMaskRequired =
        !debugRoute.active && stylizedFeatureMaskBackendReady && POST::PostSystem::RequiresGlobalSurfaceFeatureMask();
    const bool actionPostMaskRequired =
        !debugRoute.active && stylizedFeatureMaskBackendReady && POST::PostSystem::RequiresActionSurfaceFeatureMask();
    RENDER3D::SCREENSPACE::ScreenSpaceSurfaceRequirements screenSurfaceRequirements{};
    screenSurfaceRequirements.requirePackedSurface =
        stylizedScreenMaskRequired || globalPostMaskRequired || actionPostMaskRequired || hasSsrWork;
    screenSurfaceRequirements.requireFeatureMask =
        stylizedScreenMaskRequired || globalPostMaskRequired || actionPostMaskRequired;
    screenSurfaceRequirements.requireCurrentFrameAo = camera.IsOrthographic();
    const bool volumetricFogReady = RENDER3D::VOLUMETRIC::PrepareVolumetricLightingFrame(
        frame.graphics, temporalFrame, frame.scene.color, frame.scene.depth, environment,
        MESHRENDERER::GetLocalLightListGpuBindings(), renderQuality.volumetricLightingQuality,
        volumetricDebugView ? debugView : RenderDebugView::None,
        SHADOW::GetLocalLightShadowFrame(frame.graphics, lighting, camera), &cloudSolarVisibility,
        view.localFogVolumes);
    if (!MESHRENDERER::FinalizeVolumetricFog(frame.graphics, volumetricFogReady)) {
        MESHRENDERER::EndFrame();
        return false;
    }

    screenSpaceContext.jitteredViewProj = temporalFrame.camera.jittered;
    const MESHRENDERER::CameraCB* cameraCb = MESHRENDERER::GetCameraConstants();
    const MESHRENDERER::CameraCB* cullingCameraCb = MESHRENDERER::GetGpuDrivenCullingCameraConstants();

    RENDER3D::SCREENSPACE::ScreenSpaceFrameResult screenResult{};
    if (cameraCb != nullptr && cullingCameraCb != nullptr) {
        screenResult = RENDER3D::SCREENSPACE::ExecuteScreenSpacePreLightingPasses(
            RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(), screenSpaceContext, *cameraCb, *cullingCameraCb,
            environment, screenSurfaceRequirements);
    } else {
        RENDER3D::SCREENSPACE::EnsureScreenSpaceFallbacks(RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState());
        screenResult.fallbackAoTextureHandle =
            RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState().fallbackAoTextureHandle;
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(frame.graphics);
    }

    // 不透明 mainline を先に scene DSV へ深度だけ描き、ForwardOpaque
    // (LESS_EQUAL) のピクセル過描画を early-Z で殺す。visibility 用の
    // occluder prepass (別ターゲット) とは独立している。
    // 正交 HD2D は再投影なしの前フレーム AO を使用しない。
    // GeometryAux がなければ既存の depth-only pass で現フレームを供給する。
    const bool orthographicAoNeedsDepth =
        screenSurfaceRequirements.requireCurrentFrameAo &&
        RENDER3D::SCREENSPACE::ResolveEffectiveSsaoMode(environment.ambientOcclusion) == SsaoMode::Balanced;
    const bool scenePrepassEnabled =
        orthographicAoNeedsDepth || ShouldRunSceneDepthPrepass(RENDER3D::GetRenderQualitySettings());
    const bool sceneDsvReady = screenSpaceContext.sceneDsv.ptr != 0;
    const bool depthPrepassWorkReady = MESHRENDERER::HasDepthPrepassWork();
    if (scenePrepassEnabled && !screenResult.geometryAuxWritten && sceneDsvReady && depthPrepassWorkReady) {
        bool prepassExecuted = false;
        {
            GFX::PIX::ScopedGpuEvent pixScenePrepass(frame.graphics.cmdList, GFX::PIX::kColorRender,
                                                     "SceneDepthPrepass");
            GFX::GPU_PROFILE::ScopedGpuTimer gpuScenePrepass(frame.graphics.cmdList,
                                                             GFX::GPU_PROFILE::Pass::DepthPrepass);
            prepassExecuted = MESHRENDERER::RenderDepthPrepass(frame.graphics, screenSpaceContext.sceneDsv);
            if (!POST::PostSystem::RebindCurrentRenderTarget()) {
                MESHRENDERER::EndFrame();
                return false;
            }
        }
        // 最初のフレームの状態だけを boot ヘルスチェックとして記録する。
        static bool sLoggedScenePrepassState = false;
        if (!sLoggedScenePrepassState) {
            sLoggedScenePrepassState = true;
            HIKARI_LOG_INFO(std::string("[RenderFramePipeline] scene depth prepass: ") +
                            (prepassExecuted ? "executed" : "no gpu-driven work prepared"));
        }
        // Balanced SSAO は prepass が書いた当該フレームの深度から同フレーム
        // で作る (旧 temporal 経路の 1 フレーム遅れによる引き摺りを回避)。
        if (prepassExecuted && cameraCb != nullptr) {
            (void)RENDER3D::SCREENSPACE::ExecuteBalancedSsaoFromSceneDepth(
                RENDER3D::SCREENSPACE::GetScreenSpaceRuntimeState(), screenSpaceContext, *cameraCb, environment,
                screenResult);
        }
    } else if (scenePrepassEnabled) {
        static bool sWarnedScenePrepassSkip = false;
        if (!sWarnedScenePrepassSkip) {
            sWarnedScenePrepassSkip = true;
            HIKARI_LOG_WARN(std::string("[RenderFramePipeline] scene depth prepass skipped:") +
                            (sceneDsvReady ? "" : " sceneDsv=null") +
                            (depthPrepassWorkReady ? "" : " depthPrepassSource=empty"));
        }
    }

    if (!MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(screenResult.ssaoRendered)) {
        MESHRENDERER::EndFrame();
        return false;
    }
    if (!PublishOptionalTexture(sceneTextures,
                                GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::PackedSurface,
                                                             GRAPH::RenderGraphSemanticPhase::PreOpaque},
                                screenResult.packedSurface, SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
        (screenResult.ssaoRendered &&
         !PublishOptionalTexture(sceneTextures,
                                 GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::AmbientOcclusion,
                                                              GRAPH::RenderGraphSemanticPhase::PreOpaque},
                                 screenResult.ambientOcclusion, SURFACE::SceneSurfaceProducer::Ssao)) ||
        !PublishOptionalDepthPyramid(sceneTextures,
                                     GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::VisibilityDepthPyramid,
                                                                  GRAPH::RenderGraphSemanticPhase::PreOpaque},
                                     screenResult.visibilityDepthPyramid)) {
        LogPrimaryPipelineFailure(frame.frameIndex, "screen-space surface publication");
        MESHRENDERER::EndFrame();
        return false;
    }
    MESHRENDERER::MeshPassResources opaqueResources = BuildMeshPassResources(
        sceneTextures, GRAPH::RenderGraphSemanticPhase::PreOpaque, screenResult.fallbackAoTextureHandle);
    opaqueResources.visibilityDepthPyramid = screenResult.visibilityDepthPyramid;
    MESHRENDERER::MeshPassResources postOpaqueResources = opaqueResources;
    FEATURES::RenderFeaturePlan renderFeaturePlan{};

    const RenderFrameGraphStageResult graphStage = ExecuteRenderFrameGraphStage(RenderFrameGraphStageContext{
        frame,
        screenSpaceContext,
        temporalFrame,
        temporalResources,
        environment,
        renderQuality,
        RenderFrameGraphFeatureContext{lighting, featureRequests, renderFeaturePlan, gStylizedScreenFeatures,
                                       !debugRoute.active, geometryOutlineBackendReady, visibilityOverlayBackendReady},
        cameraCb,
        screenResult,
        sceneTextures,
        opaqueResources,
        postOpaqueResources,
        debugView,
        hasDepthAwareWork,
        hasTransparentWork,
        hasSsrWork,
        hasSubsurfaceWork,
        hasDepthAwareWork || hasTransparentWork || hasSsrWork || stylizedScreenMaskRequired || globalPostMaskRequired,
        &rayLighting,
        &materials});
    if (!graphStage.prefixCompleted || !graphStage.pausedForPresentation || !graphStage.session.IsValid()) {
        const std::string graphDetail =
            std::string(GRAPH::ToString(graphStage.diagnostic.code)) + " " + graphStage.diagnostic.message.data();
        LogPrimaryPipelineFailure(frame.frameIndex, "render-graph prefix", graphDetail.c_str());
        MESHRENDERER::EndFrame();
        return false;
    }
    if (!POST::PostSystem::SetSceneCaptureGraphSession(graphStage.session)) {
        (void)AbortRenderFrameGraphSession(graphStage.session);
        MESHRENDERER::EndFrame();
        return false;
    }
    MESHRENDERER::EndFrame();
    surfaceFrameGuard.HandOff();
    return true;
}

bool RenderMeshCaptureOpaqueFrame(const GFX::Context& graphics, const Camera3D& camera,
                                  const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                                  uint32_t width, uint32_t height) {
    if (!MESHRENDERER::HasSubmittedItems()) {
        return true;
    }

    GFX::PIX::ScopedGpuEvent pixFrame(graphics.cmdList, GFX::PIX::kColorRender,
                                      "RenderFrame.ReflectionProbeCaptureOpaque");

    if (!MESHRENDERER::BeginFrame(graphics, camera, environment, lighting, width, height)) {
        MESHRENDERER::EndFrame();
        return false;
    }
    (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(graphics);

    if (!MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(false)) {
        MESHRENDERER::EndFrame();
        return false;
    }

    // Capture は後処理と depth-aware phase を含めない。
    const MESHRENDERER::MeshPassResources captureResources{};
    const bool opaqueOk = MESHRENDERER::RenderForwardOpaquePass(graphics, captureResources);
    MESHRENDERER::EndFrame();
    return opaqueOk;
}

void ShutdownRenderFramePipelineResources() noexcept {
    try {
        gDynamicDiffuseGi.Reset(GFX::GetCurrentContext());
        gRuntimeRayLighting.Reset(GFX::GetCurrentContext());
        gRuntimeRayScene.Reset(GFX::GetCurrentContext());
        gStylizedScreenFeatures.Release();
    } catch (...) {
    }
    INTERNAL::ReleaseRenderFrameGraphPassResources();
}

const RAYTRACING::RuntimeRayTracingStats& GetRuntimeRayTracingStats() noexcept { return gRuntimeRayScene.GetStats(); }

const DYNAMICGI::Stats& GetDynamicDiffuseGiStats() noexcept { return gDynamicDiffuseGi.GetStats(); }
uint64_t GetPrimaryFrameFailureCount() noexcept { return gPrimaryFrameFailureCount; }

} // namespace HIKARI::RENDER3D::PIPELINE
