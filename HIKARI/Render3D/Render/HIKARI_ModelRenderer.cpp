#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

namespace HIKARI::MODELRENDERER {

    namespace {
        ModelRendererDebugStats gDebugStats{};
        RENDER3D::RUNTIME::RenderModelCache gRenderModelCache{};

        void SyncRenderModelCacheStats() {
            const RENDER3D::RUNTIME::RenderModelCache::Stats& cacheStats =
                gRenderModelCache.GetStats();
            gDebugStats.cache.renderModelCacheRequestCount = cacheStats.requestCount;
            gDebugStats.cache.renderModelCacheHitCount = cacheStats.hitCount;
            gDebugStats.cache.renderModelCacheMissCount = cacheStats.missCount;
            gDebugStats.cache.renderModelCacheInvalidCount = cacheStats.invalidModelCount;
            gDebugStats.cache.renderModelCachedModelCount = cacheStats.cachedModelCount;
            gDebugStats.cache.renderModelCachedSurfaceCount = cacheStats.cachedSurfaceCount;
        }
    }

    const char* ToString(ModelRendererFrameKind kind) {
        switch (kind) {
        case ModelRendererFrameKind::MainView:
            return "MainView";
        case ModelRendererFrameKind::ReflectionProbeCapture:
            return "ReflectionProbeCapture";
        case ModelRendererFrameKind::LightProbeCapture:
            return "LightProbeCapture";
        case ModelRendererFrameKind::None:
        default:
            return "None";
        }
    }

    void ResetModelRendererFrameStats() {
        gDebugStats.frame = {};
    }

    void ResetFrame() {
        ResetModelRendererFrameStats();
        gDebugStats.frameKind = ModelRendererFrameKind::None;
        SyncRenderModelCacheStats();
    }

    void BeginModelRendererFrame(ModelRendererFrameKind kind) {
        ResetModelRendererFrameStats();
        gDebugStats.frameKind = kind;
        SyncRenderModelCacheStats();
    }

    void Reset() {
        ResetFrame();
        MESHRENDERER::Reset();
        SHADOW::Reset();
    }

    void RenderAll(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return;
        }

        const Camera3D& camera = view.cameraFrame->camera;
        BeginModelRendererFrame(ModelRendererFrameKind::MainView);
        SHADOW::BeginFrame(environment, camera);
        SHADOW::RenderDirectionalShadowMap();
        MESHRENDERER::RenderAll(view, environment, debugView);
    }

    void RenderOpaqueForReflectionProbeCapture(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height,
        ModelRendererFrameKind kind) {

        BeginModelRendererFrame(kind);
        (void)RENDER3D::PIPELINE::RenderMeshCaptureOpaqueFrame(
            camera,
            environment,
            width,
            height);
    }

    const ModelRendererDebugStats& GetDebugStats() {
        SyncRenderModelCacheStats();
        return gDebugStats;
    }

    RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache() {
        return gRenderModelCache;
    }

    const RENDER3D::RUNTIME::RenderModelCache::Stats& GetRenderModelCacheStats() {
        return gRenderModelCache.GetStats();
    }

}
