#pragma once

#include <cstddef>
#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {
class ModelManager;
class SkyManager;
class RenderTarget2D;
} // namespace HIKARI

namespace HIKARI::MODELRENDERER {

enum class ModelRendererFrameKind {
    None,
    MainView,
    ReflectionProbeCapture,
    LightProbeCapture,
    SecondarySceneCapture,
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

struct SecondarySceneCaptureOutput {
    RENDER3D::TextureResourceHandle texture{};
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t outputRevision = 0u;
    bool ready = false;
};

void Reset();
void ResetFrame();
void ResetModelRendererFrameStats();
void BeginModelRendererFrame(ModelRendererFrameKind kind);
MESHRENDERER::MeshRendererFrameResult RenderAll(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                                const SceneEnvironment& environment,
                                                const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                RenderDebugView debugView = RenderDebugView::None);
void RenderOpaqueForReflectionProbeCapture(
    const GFX::Context& graphics, const Camera3D& camera, const SceneEnvironment& environment,
    const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t width, uint32_t height,
    ModelRendererFrameKind kind = ModelRendererFrameKind::ReflectionProbeCapture);
bool RenderSecondarySceneCapture(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                 const SceneEnvironment& environment,
                                 const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t captureSlot,
                                 uint32_t width, uint32_t height, bool renderTransparent,
                                 RenderTarget2D* target = nullptr);
bool RenderSecondarySceneCaptureTarget(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                       const SceneEnvironment& environment,
                                       const RENDER3D::LIGHTING::SceneLightingSet& lighting, ModelManager& modelManager,
                                       SkyManager& skyManager, uint64_t worldIdentity, uint32_t captureSlot,
                                       uint32_t width, uint32_t height, bool renderSky, bool renderTransparent,
                                       SecondarySceneCaptureOutput& outOutput);
void ResetSecondarySceneCaptureTarget(uint32_t captureSlot) noexcept;
void ResetSecondarySceneCaptureTarget() noexcept;
const ModelRendererDebugStats& GetDebugStats();
RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache();

} // namespace HIKARI::MODELRENDERER
