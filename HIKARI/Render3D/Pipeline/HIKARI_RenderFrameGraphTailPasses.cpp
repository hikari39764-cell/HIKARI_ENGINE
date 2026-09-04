#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <cstddef>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Features/HIKARI_RenderFeaturePlanner.h"
#include "Render3D/Features/HIKARI_StylizedScreenFeatureRenderer.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Render3D/Water/HIKARI_WaterVisibleDepth.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

    bool ExecuteDepthAware(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (!context.hasDepthAwareWork) {
            return true;
        }
        bool depthReadActive = false;
        try {
            GFX::PIX::ScopedGpuEvent event(
                context.frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "DepthAware");
            GFX::GPU_PROFILE::ScopedGpuTimer timer(
                context.frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::DepthAware);
            if (!context.screenSpace.renderTargetAccess.BeginDepthRead()) {
                return false;
            }
            depthReadActive = true;
            const bool rendered = MESHRENDERER::RenderDepthAwarePass(
                context.frame.graphics,
                context.postOpaqueResources);
            if (rendered &&
                VOLUMETRIC::IsVolumetricLightingActive()) {
                GFX::PIX::ScopedGpuEvent waterEvent(
                    context.frame.graphics.cmdList,
                    GFX::PIX::kColorRender,
                    "WaterVisibilityDepth");
                GFX::GPU_PROFILE::ScopedGpuTimer waterTimer(
                    context.frame.graphics.cmdList,
                    GFX::GPU_PROFILE::Pass::WaterVisibilityDepth);
                const WATER::WaterVisibleDepthView waterDepth =
                    WATER::RenderWaterVisibleDepth(
                        context.frame.graphics,
                        context.postOpaqueResources,
                        context.postOpaqueResources.sceneDepth.width,
                        context.postOpaqueResources.sceneDepth.height,
                        context.frame.frameIndex,
                        context.postOpaqueResources.sceneDepth.resource);
                if (waterDepth.IsValid()) {
                    VOLUMETRIC::SetVolumetricVisibleWaterDepth(waterDepth);
                }
            }
            context.screenSpace.renderTargetAccess.EndDepthRead();
            depthReadActive = false;
            return rendered;
        }
        catch (...) {
            if (depthReadActive) {
                context.screenSpace.renderTargetAccess.EndDepthRead();
            }
            return false;
        }
    }

    bool ExecuteForwardTransparent(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (!context.hasTransparentWork) {
            return true;
        }
        try {
            GFX::PIX::ScopedGpuEvent event(
                context.frame.graphics.cmdList,
                GFX::PIX::kColorRender,
                "ForwardTransparent");
            GFX::GPU_PROFILE::ScopedGpuTimer timer(
                context.frame.graphics.cmdList,
                GFX::GPU_PROFILE::Pass::ForwardTransparent);
            return MESHRENDERER::RenderForwardTransparentPass(
                context.frame.graphics,
                context.postOpaqueResources);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteRenderFeaturePlan(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        try {
            FEATURES::RenderFeatureAvailability availability{};
            availability.width = context.frame.renderWidth;
            availability.height = context.frame.renderHeight;
            availability.enabledMask = context.features.enabled
                ? 0xffffffffu
                : 0u;
            if (context.features.geometryBackendReady) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::GeometryBackend);
            }
            if (context.features.visibilityBackendReady) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::VisibilityBackend);
            }
            if (context.frame.targetAccess.rebind != nullptr) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneColor);
            }
            if (context.sceneSurfaces.TryGetTexture(
                    GRAPH::RenderGraphSurfaceKey{
                        GRAPH::RenderGraphSemantic::SceneDepth,
                        GRAPH::RenderGraphSemanticPhase::Imported }) !=
                    nullptr) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SceneDepth);
            }
            if (context.sceneSurfaces.TryGetTexture(
                    GRAPH::RenderGraphSurfaceKey{
                        GRAPH::RenderGraphSemantic::PackedSurface,
                        GRAPH::RenderGraphSemanticPhase::PostOpaque }) !=
                    nullptr) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceNormals);
            }
            if (context.sceneSurfaces.TryGetTexture(
                    GRAPH::RenderGraphSurfaceKey{
                        GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
                        GRAPH::RenderGraphSemanticPhase::PostOpaque }) !=
                    nullptr) {
                availability.inputMask |= FEATURES::RenderFeatureInputBit(
                    FEATURES::RenderFeatureInput::SurfaceFeatureMask);
            }
            context.features.plan = FEATURES::BuildRenderFeaturePlan(
                context.features.requests,
                availability);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteGeometryFeatures(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        const auto& entry = context.features.plan.entries[
            static_cast<std::size_t>(
                FEATURES::RenderFeatureKind::GeometryOutline)];
        if (!entry.scheduled || entry.compatibilityFallback) {
            return true;
        }
        try {
            return context.frame.targetAccess.Rebind() &&
                MESHRENDERER::RenderGeometryOutlineFeaturePass(
                    context.frame.graphics,
                    context.postOpaqueResources);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteVisibilityFeatures(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        const auto& entry = context.features.plan.entries[
            static_cast<std::size_t>(
                FEATURES::RenderFeatureKind::VisibilityOverlay)];
        if (!entry.scheduled) {
            return true;
        }
        try {
            return context.frame.targetAccess.Rebind() &&
                MESHRENDERER::RenderVisibilityOverlayFeaturePass(
                    context.frame.graphics,
                    context.postOpaqueResources);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteSceneColorPostOpaqueCapture(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (!context.features.plan.screenCompositePassRequired) {
            return true;
        }
        try {
            const RenderTextureView snapshot =
                POST::PostSystem::CaptureSceneColorSnapshot(
                    POST::SceneColorSnapshotKind::PostOpaque);
            return snapshot.IsSampleable() &&
                context.sceneSurfaces.PublishTexture(
                    GRAPH::RenderGraphSurfaceKey{
                        GRAPH::RenderGraphSemantic::SceneColorPostOpaque,
                        GRAPH::RenderGraphSemanticPhase::PostOpaque },
                    snapshot,
                    SURFACE::SceneSurfaceProducer::SceneColorCapture);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteStylizedFeatureComposite(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        if (!context.features.plan.screenCompositePassRequired) {
            return true;
        }
        try {
            FEATURES::StylizedScreenFeatureSettings settings{};
            if (context.cameraConstants != nullptr) {
                settings.viewProj = context.cameraConstants->viewProj;
            }
            if (const LIGHTING::SceneLightProvider* directional =
                    LIGHTING::SelectDirectionalLight(
                        context.features.lighting)) {
                settings.directionalLightWS[0] = directional->direction.x;
                settings.directionalLightWS[1] = directional->direction.y;
                settings.directionalLightWS[2] = directional->direction.z;
            }
            else {
                settings.directionalLightWS[0] =
                    context.environment.directional.direction.x;
                settings.directionalLightWS[1] =
                    context.environment.directional.direction.y;
                settings.directionalLightWS[2] =
                    context.environment.directional.direction.z;
            }
            return context.features.renderer.Render(
                context.frame.graphics,
                context.frame.targetAccess,
                context.sceneSurfaces,
                context.features.plan,
                settings);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecuteSceneColorFinalCapture(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
            return false;
        }
        const RenderFrameGraphStageContext& context = *state.context;
        try {
            const RenderTextureView snapshot =
                POST::PostSystem::CaptureSceneColorSnapshot(
                    POST::SceneColorSnapshotKind::Final);
            return snapshot.IsSampleable() &&
                context.sceneSurfaces.PublishTexture(
                    GRAPH::RenderGraphSurfaceKey{
                        GRAPH::RenderGraphSemantic::SceneColorFinal,
                        GRAPH::RenderGraphSemanticPhase::Final },
                    snapshot,
                    SURFACE::SceneSurfaceProducer::PostFinal);
        }
        catch (...) {
            return false;
        }
    }

    bool ExecutePresentationResume(
        const GRAPH::RenderGraphPassContext& graphPass,
        void* userData) noexcept {
        // This first suffix is intentionally a session-owned marker. It must
        // not access the Pipeline stack or claim a SceneColorFinal producer.
        if (userData == nullptr) {
            return false;
        }
        auto& state = *static_cast<StageExecutionState*>(userData);
        if (state.context != nullptr ||
            !state.session.IsValid() ||
            !graphPass.frame.Matches(state.session.surfaces.frame) ||
            TryGetRenderFrameGraph(state.session) != graphPass.graph) {
            return false;
        }
        state.presentationResumed = true;
        return true;
    }

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
