#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"

namespace HIKARI::MODELRENDERER {

    enum class ModelRendererFrameKind {
        None,
        MainView,
        ReflectionProbeCapture,
        LightProbeCapture,
    };

    const char* ToString(ModelRendererFrameKind kind);

    struct ModelRendererFrameStats {
        uint32_t reserved = 0;
    };

    struct ModelRendererCacheStats {
        uint32_t renderModelCacheRequestCount = 0;
        uint32_t renderModelCacheHitCount = 0;
        uint32_t renderModelCacheMissCount = 0;
        uint32_t renderModelCacheInvalidCount = 0;
        uint32_t renderModelCachedModelCount = 0;
        uint32_t renderModelCachedSurfaceCount = 0;
    };

    struct ModelRendererDebugStats {
        ModelRendererFrameKind frameKind = ModelRendererFrameKind::None;
        ModelRendererFrameStats frame{};
        ModelRendererCacheStats cache{};
    };

    void Reset();
    void ResetFrame();
    void ResetModelRendererFrameStats();
    void BeginModelRendererFrame(ModelRendererFrameKind kind);
    void RenderAll(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
    void RenderOpaqueForReflectionProbeCapture(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height,
        ModelRendererFrameKind kind = ModelRendererFrameKind::ReflectionProbeCapture);
    const ModelRendererDebugStats& GetDebugStats();
    RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache();
    const RENDER3D::RUNTIME::RenderModelCache::Stats& GetRenderModelCacheStats();

}
