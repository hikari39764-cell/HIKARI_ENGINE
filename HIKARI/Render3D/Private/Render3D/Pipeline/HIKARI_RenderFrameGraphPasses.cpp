#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Pipeline/HIKARI_ProjectComputeShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_ProjectFullscreenShaderRuntime.h"
#include "Render3D/Reflection/HIKARI_ScreenSpaceReflectionRenderer.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/ScreenSpace/HIKARI_PathTracedReferenceRenderer.h"
#include "Render3D/ScreenSpace/HIKARI_SubsurfaceScatteringRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {
namespace {
REFLECTION::ScreenSpaceReflectionRenderer gSsrRenderer{};
SCREENSPACE::SubsurfaceScatteringRenderer gSubsurfaceRenderer{};
SCREENSPACE::PathTracedReferenceRenderer gPathTracedRenderer{};

bool ReplaceTexturePublication(
    SURFACE::SceneSurfaceSet& surfaces, GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
    SURFACE::SceneSurfaceProducer producer,
    SURFACE::SceneSurfaceAvailability availability = SURFACE::SceneSurfaceAvailability::Produced,
    GRAPH::RenderGraphSurfaceKey aliasSource = {}) noexcept {
    (void)surfaces.Invalidate(key);
    if (!texture.IsSampleable()) {
        return true;
    }
    const RenderTextureView* source = surfaces.TryGetTexture(aliasSource);
    const bool sameStorage = source != nullptr && source->format == texture.format && source->width == texture.width &&
                             source->height == texture.height && source->mipCount == texture.mipCount &&
                             source->MatchesPhysicalView(texture);
    return sameStorage ? surfaces.PublishTextureAlias(key, aliasSource, producer)
                       : surfaces.PublishTexture(key, texture, producer, availability);
}

bool ReplaceDepthPyramidPublication(SURFACE::SceneSurfaceSet& surfaces, GRAPH::RenderGraphSurfaceKey key,
                                    const DEPTH::DepthPyramidView& pyramid,
                                    SURFACE::SceneSurfaceProducer producer) noexcept {
    (void)surfaces.Invalidate(key);
    return !pyramid.valid || surfaces.PublishDepthPyramid(key, pyramid, producer);
}
} // namespace

bool ValidateRenderFrameGraphPassContext(const GRAPH::RenderGraphPassContext& graphPass,
                                         const StageExecutionState& state) noexcept {
    if (state.context == nullptr) {
        return false;
    }
    const RenderFrameGraphStageContext& stage = *state.context;
    return graphPass.frame.Matches(stage.frame.token) && stage.temporalFrame.publication.Matches(stage.frame.token) &&
           stage.temporalFrame.view.viewId == stage.frame.viewId && stage.temporalResources.IsValid() &&
           stage.temporalResources.view.Matches(stage.temporalFrame.view) &&
           stage.temporalResources.publication.Matches(stage.frame.token) &&
           stage.sceneSurfaces.MatchesFrame(stage.frame.token) &&
           stage.sceneSurfaces.GetDeviceGeneration() == stage.frame.graphics.deviceGeneration;
}

bool ExecuteForwardOpaque(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const auto& context = *state.context;
    auto& buffer = SCREENSPACE::GetScreenSpaceRuntimeState().reflectionBuffer;
    constexpr GRAPH::RenderGraphSurfaceKey fallbackKey{GRAPH::RenderGraphSemantic::ReflectionFallbackSpecular,
                                                       GRAPH::RenderGraphSemanticPhase::Opaque};
    constexpr GRAPH::RenderGraphSurfaceKey responseKey{GRAPH::RenderGraphSemantic::ReflectionSpecularResponse,
                                                       GRAPH::RenderGraphSemanticPhase::Opaque};
    constexpr GRAPH::RenderGraphSurfaceKey subsurfaceKey{GRAPH::RenderGraphSemantic::SubsurfaceDiffuse,
                                                         GRAPH::RenderGraphSemanticPhase::Opaque};
    (void)context.sceneSurfaces.Invalidate(fallbackKey);
    (void)context.sceneSurfaces.Invalidate(responseKey);
    (void)context.sceneSurfaces.Invalidate(subsurfaceKey);
    bool beganCapture = false;
    try {
        const auto& frame = context.frame;
        // capture（追加 RT）は SSR か次表面散乱のどちらかが要る時に有効。capture の PSO は Full だけを持つので、
        // 前向きの診断モード（何も省かない GeneralFull を含む）の間は使わない（診断どうしの出力をそろえる）。
        const bool eligible =
            (context.hasSsrWork || context.hasSubsurfaceWork) && context.cameraConstants &&
            context.renderQuality.forwardCostMode == ForwardShadingCostMode::Full &&
            (context.debugView == RenderDebugView::None || context.debugView == RenderDebugView::SsrConfidence ||
             context.debugView == RenderDebugView::SsrHit) &&
            frame.targetAccess.bindAdditionalColors;
        MESHRENDERER::MeshPassResources resources = context.opaqueResources;
        resources.reflectionCapture = false;
        resources.subsurfaceCapture = false;
        // 任意の capture 準備に失敗しても、通常の Forward 描画は続ける。
        try {
            const bool captureSubsurface = context.hasSubsurfaceWork;
            const bool captureReady =
                eligible && MESHRENDERER::PrepareForwardReflectionCapture(frame.graphics, captureSubsurface);
            if (buffer.Ensure(frame.graphics, frame.renderWidth, frame.renderHeight, captureReady, captureSubsurface)) {
                beganCapture = buffer.BeginWrite(frame.graphics.cmdList);
                resources.reflectionCapture = beganCapture && frame.targetAccess.BindAdditionalColors(buffer.GetRtvs());
                resources.subsurfaceCapture = resources.reflectionCapture && captureSubsurface;
            }
        } catch (...) {
            resources.reflectionCapture = false;
            resources.subsurfaceCapture = false;
        }
        if (beganCapture && !resources.reflectionCapture) {
            buffer.EndWrite(frame.graphics.cmdList, false);
            beganCapture = false;
            if (!frame.targetAccess.Rebind())
                return false;
        }
        GFX::PIX::ScopedGpuEvent pixForward(frame.graphics.cmdList, GFX::PIX::kColorRender, "ForwardOpaque");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuForward(frame.graphics.cmdList, GFX::GPU_PROFILE::Pass::ForwardOpaque);
        state.result.opaqueSucceeded = MESHRENDERER::RenderForwardOpaquePass(frame.graphics, resources);
        if (beganCapture) {
            buffer.EndWrite(frame.graphics.cmdList, state.result.opaqueSucceeded);
            beganCapture = false;
            if (!frame.targetAccess.Rebind())
                return false;
            if (state.result.opaqueSucceeded &&
                (!ReplaceTexturePublication(context.sceneSurfaces, fallbackKey, buffer.GetFallbackSpecularView(),
                                            SURFACE::SceneSurfaceProducer::ForwardOpaque) ||
                 !ReplaceTexturePublication(context.sceneSurfaces, responseKey, buffer.GetSpecularResponseView(),
                                            SURFACE::SceneSurfaceProducer::ForwardOpaque))) {
                (void)context.sceneSurfaces.Invalidate(fallbackKey);
                (void)context.sceneSurfaces.Invalidate(responseKey);
            }
            if (state.result.opaqueSucceeded && resources.subsurfaceCapture &&
                buffer.GetSubsurfaceView().IsSampleable())
                (void)ReplaceTexturePublication(context.sceneSurfaces, subsurfaceKey, buffer.GetSubsurfaceView(),
                                                SURFACE::SceneSurfaceProducer::ForwardOpaque);
        }
        // 路径追跡の参照は Forward が描いた不透明面の色を置き換える（空と、後で重ねる半透明はそのまま）。
        if (state.result.opaqueSucceeded && IsPathTracedRenderDebugView(context.debugView)) {
            const SCREENSPACE::PathTracedReferenceInputs pathInputs{
                &context.temporalFrame, context.rayLighting,
                context.materials,      frame.renderWidth,
                frame.renderHeight,     context.debugView == RenderDebugView::PathTracedIndirectDiffuse};
            if (!gPathTracedRenderer.Render(frame.graphics, frame.targetAccess, pathInputs) &&
                !frame.targetAccess.Rebind())
                return false;
        }
        return state.result.opaqueSucceeded;
    } catch (...) {
        if (beganCapture) {
            buffer.EndWrite(context.frame.graphics.cmdList, false);
            try {
                (void)context.frame.targetAccess.Rebind();
            } catch (...) {
            }
        }
        state.result.opaqueSucceeded = false;
        return false;
    }
}

bool ExecutePostOpaqueSurfaces(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    if (context.cameraConstants == nullptr) {
        return false;
    }
    try {
        if (!SCREENSPACE::ExecuteScreenSpacePostOpaquePasses(SCREENSPACE::GetScreenSpaceRuntimeState(),
                                                             context.screenSpace, *context.cameraConstants,
                                                             context.environment, context.screenResult)) {
            return false;
        }
        if (!MESHRENDERER::SetAmbientOcclusionRuntimeEnabled(context.screenResult.ssaoRendered))
            return false;
        (void)context.sceneSurfaces.Invalidate(GRAPH::RenderGraphSurfaceKey{
            GRAPH::RenderGraphSemantic::VisibilityDepthPyramid, GRAPH::RenderGraphSemanticPhase::PreOpaque});
        if (!ReplaceTexturePublication(context.sceneSurfaces,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::PackedSurface,
                                                                    GRAPH::RenderGraphSemanticPhase::PostOpaque},
                                       context.screenResult.packedSurface,
                                       SURFACE::SceneSurfaceProducer::SceneSurfaceAux,
                                       SURFACE::SceneSurfaceAvailability::Produced,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::PackedSurface,
                                                                    GRAPH::RenderGraphSemanticPhase::PreOpaque}) ||
            !ReplaceTexturePublication(context.sceneSurfaces,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
                                                                    GRAPH::RenderGraphSemanticPhase::PostOpaque},
                                       context.screenResult.surfaceFeatureMask,
                                       SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
            !ReplaceTexturePublication(context.sceneSurfaces,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::StylizedOutlineStyle,
                                                                    GRAPH::RenderGraphSemanticPhase::PostOpaque},
                                       context.screenResult.stylizedOutlineStyle,
                                       SURFACE::SceneSurfaceProducer::SceneSurfaceAux) ||
            (context.screenResult.ssaoRendered
                 ? !ReplaceTexturePublication(context.sceneSurfaces,
                                              GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::AmbientOcclusion,
                                                                           GRAPH::RenderGraphSemanticPhase::PostOpaque},
                                              context.screenResult.ambientOcclusion,
                                              SURFACE::SceneSurfaceProducer::Ssao,
                                              SURFACE::SceneSurfaceAvailability::Produced,
                                              GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::AmbientOcclusion,
                                                                           GRAPH::RenderGraphSemanticPhase::PreOpaque})
                 : !context.sceneSurfaces.Invalidate(GRAPH::RenderGraphSurfaceKey{
                       GRAPH::RenderGraphSemantic::AmbientOcclusion, GRAPH::RenderGraphSemanticPhase::PostOpaque})) ||
            !ReplaceDepthPyramidPublication(
                context.sceneSurfaces,
                GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::CurrentDepthPyramid,
                                             GRAPH::RenderGraphSemanticPhase::PostOpaque},
                context.screenResult.currentSceneDepthPyramid, SURFACE::SceneSurfaceProducer::DepthPyramid) ||
            !ReplaceDepthPyramidPublication(
                context.sceneSurfaces,
                GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::VisibilityDepthPyramid,
                                             GRAPH::RenderGraphSemanticPhase::PostOpaque},
                context.screenResult.visibilityDepthPyramid, SURFACE::SceneSurfaceProducer::DepthPyramid)) {
            return false;
        }
        context.postOpaqueResources =
            BuildMeshPassResources(context.sceneSurfaces, GRAPH::RenderGraphSemanticPhase::PostOpaque,
                                   context.screenResult.fallbackAoTextureHandle);
        return true;
    } catch (...) {
        return false;
    }
}

bool ExecuteClouds(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    const bool supportedPurpose =
        context.frame.purpose == RenderViewPurpose::Game || context.frame.purpose == RenderViewPurpose::EditorScene;
    if (!CLOUDS::HasVisibleClouds(context.environment) || !supportedPurpose) {
        return true;
    }
    try {
        return CLOUDS::ExecuteVolumetricCloudStage(context.frame, context.temporalFrame, context.environment,
                                                   context.renderQuality.volumetricCloudQuality);
    } catch (...) {
        return false;
    }
}

bool ExecuteMotionVectors(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    bool depthReadActive = false;
    try {
        TEMPORAL::TemporalInputs temporalInputs{};
        const RenderTextureView* sceneDepth = context.sceneSurfaces.TryGetTexture(GRAPH::RenderGraphSurfaceKey{
            GRAPH::RenderGraphSemantic::SceneDepth, GRAPH::RenderGraphSemanticPhase::Imported});
        const RenderTextureView* sceneColorOpaque = context.sceneSurfaces.TryGetTexture(GRAPH::RenderGraphSurfaceKey{
            GRAPH::RenderGraphSemantic::SceneColorOpaque, GRAPH::RenderGraphSemanticPhase::Opaque});
        const RenderTextureView noSceneColor{};
        if (sceneDepth == nullptr) {
            return false;
        }
        if (!TEMPORAL::TryBuildTemporalPassInputs(context.temporalResources, *sceneDepth,
                                                  sceneColorOpaque != nullptr ? *sceneColorOpaque : noSceneColor,
                                                  temporalInputs)) {
            state.result.motionVectorsWritten = false;
            return false;
        }
        bool written = false;
        if (temporalInputs.motionVectors.valid && sceneDepth->IsSampleable() &&
            context.screenSpace.renderTargetAccess.BeginDepthRead()) {
            depthReadActive = true;
            written = TEMPORAL::ExecuteMotionVectorPass(context.frame.graphics, temporalInputs);
            if (written) {
                written = context.screenSpace.readOnlySceneDsv.ptr != 0u &&
                          TEMPORAL::ExecuteTemporalGeometryPass(context.frame.graphics, temporalInputs,
                                                                context.screenSpace.readOnlySceneDsv);
            }
            context.screenSpace.renderTargetAccess.EndDepthRead();
            depthReadActive = false;
            if (!context.screenSpace.renderTargetAccess.Rebind()) {
                return false;
            }
        }

        if (!ReplaceTexturePublication(context.sceneSurfaces,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::MotionVectors,
                                                                    GRAPH::RenderGraphSemanticPhase::Temporal},
                                       written ? temporalInputs.motionVectors : RenderTextureView{},
                                       SURFACE::SceneSurfaceProducer::TemporalMotion) ||
            !ReplaceTexturePublication(context.sceneSurfaces,
                                       GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::MotionMetadata,
                                                                    GRAPH::RenderGraphSemanticPhase::Temporal},
                                       written ? temporalInputs.motionMetadata : RenderTextureView{},
                                       SURFACE::SceneSurfaceProducer::TemporalMotion)) {
            return false;
        }
        (void)TEMPORAL::MarkMotionVectorsWritten(context.temporalResources, written);
        state.result.motionVectorsWritten = written;
        return written;
    } catch (...) {
        if (depthReadActive) {
            context.screenSpace.renderTargetAccess.EndDepthRead();
        }
        (void)context.sceneSurfaces.Invalidate(GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::MotionVectors,
                                                                            GRAPH::RenderGraphSemanticPhase::Temporal});
        (void)context.sceneSurfaces.Invalidate(GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::MotionMetadata,
                                                                            GRAPH::RenderGraphSemanticPhase::Temporal});
        (void)TEMPORAL::MarkMotionVectorsWritten(context.temporalResources, false);
        state.result.motionVectorsWritten = false;
        return false;
    }
}

bool ExecuteSceneColorOpaqueCapture(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    try {
        const RenderTextureView sceneColorOpaque =
            POST::PostSystem::CaptureSceneColorSnapshot(POST::SceneColorSnapshotKind::Opaque);
        if (!sceneColorOpaque.IsSampleable()) {
            return false;
        }
        if (!context.sceneSurfaces.PublishTexture(
                GRAPH::RenderGraphSurfaceKey{GRAPH::RenderGraphSemantic::SceneColorOpaque,
                                             GRAPH::RenderGraphSemanticPhase::Opaque},
                sceneColorOpaque, SURFACE::SceneSurfaceProducer::SceneColorCapture)) {
            return false;
        }
        context.postOpaqueResources.opaqueSceneColor = sceneColorOpaque;
        if (!TEMPORAL::SetTemporalCompositionBase(context.temporalResources, sceneColorOpaque.srv)) {
            return false;
        }
        state.result.sceneColorOpaquePublished = true;
        return POST::PostSystem::RebindCurrentRenderTarget();
    } catch (...) {
        return false;
    }
}

bool ExecuteSsr(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    if (context.cameraConstants == nullptr) {
        return true;
    }
    if (!context.sceneSurfaces.HasOpaqueSurface() || !context.sceneSurfaces.HasCurrentDepthHierarchy() ||
        !context.sceneSurfaces.TryGetTexture(
            {GRAPH::RenderGraphSemantic::ReflectionFallbackSpecular, GRAPH::RenderGraphSemanticPhase::Opaque}) ||
        !context.sceneSurfaces.TryGetTexture(
            {GRAPH::RenderGraphSemantic::ReflectionSpecularResponse, GRAPH::RenderGraphSemanticPhase::Opaque})) {
        return true;
    }
    try {
        if (gSsrRenderer.Render(
                context.frame.graphics, context.frame.targetAccess, context.sceneSurfaces, *context.cameraConstants,
                context.environment.screenSpaceReflections, context.debugView, context.temporalFrame,
                context.renderQuality.rayTracedReflections ? context.rayLighting : nullptr, context.materials))
            return true;
        return context.frame.targetAccess.Rebind();
    } catch (...) {
        try {
            context.frame.targetAccess.EndDepthRead();
            return context.frame.targetAccess.Rebind();
        } catch (...) {
            return false;
        }
    }
}

bool ExecuteSubsurfaceScattering(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    if (context.cameraConstants == nullptr || context.debugView != RenderDebugView::None) {
        return true;
    }
    const RenderTextureView* diffuse = context.sceneSurfaces.TryGetTexture(
        {GRAPH::RenderGraphSemantic::SubsurfaceDiffuse, GRAPH::RenderGraphSemanticPhase::Opaque});
    const RenderTextureView* depth = context.sceneSurfaces.TryGetTexture(
        {GRAPH::RenderGraphSemantic::SceneDepth, GRAPH::RenderGraphSemanticPhase::Imported});
    if (diffuse == nullptr || depth == nullptr || !diffuse->IsSampleable() || !depth->IsSampleable()) {
        return true;
    }
    try {
        if (gSubsurfaceRenderer.Render(context.frame.graphics, context.frame.targetAccess, *diffuse, *depth,
                                       *context.cameraConstants, context.environment.subsurfaceScattering))
            return true;
        return context.frame.targetAccess.Rebind();
    } catch (...) {
        try {
            context.frame.targetAccess.EndDepthRead();
            return context.frame.targetAccess.Rebind();
        } catch (...) {
            return false;
        }
    }
}

const RenderFrameGraphPassCallbacks& GetRenderFrameGraphPassCallbacks() noexcept {
    static const RenderFrameGraphPassCallbacks callbacks{&ExecuteForwardOpaque,
                                                         &ExecutePostOpaqueSurfaces,
                                                         &ExecuteClouds,
                                                         &ExecuteMotionVectors,
                                                         &ExecuteSceneColorOpaqueCapture,
                                                         &ExecuteSsr,
                                                         &ExecuteSubsurfaceScattering,
                                                         &ExecuteDepthAware,
                                                         &ExecuteFluidVolumes,
                                                         &ExecuteForwardTransparent,
                                                         &ExecuteRenderFeaturePlan,
                                                         &ExecuteGeometryFeatures,
                                                         &ExecuteVisibilityFeatures,
                                                         &ExecuteSceneColorPostOpaqueCapture,
                                                         &ExecuteStylizedFeatureComposite,
                                                         &ExecuteCameraDepthOfField,
                                                         &ExecuteSceneColorFinalCapture,
                                                         &ExecuteProjectRenderPass,
                                                         &ExecuteProjectDispatchResolve,
                                                         &ExecuteProjectRenderObserver,
                                                         &ExecutePresentationResume};
    return callbacks;
}

void ReleaseRenderFrameGraphPassResources() noexcept {
    ReleaseProjectComputeShaderRuntime();
    ReleaseProjectFullscreenShaderRuntime();
    try {
        gSsrRenderer.Release();
        gSubsurfaceRenderer.Release();
        gPathTracedRenderer.Release();
    } catch (...) {
    }
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
