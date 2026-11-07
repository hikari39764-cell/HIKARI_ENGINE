#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "RenderCore/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {
class SkyManager;
class RenderTarget2D;
struct GameProjectSceneCaptureStyleDesc;
} // namespace HIKARI

namespace HIKARI::RENDER3D {
struct ScreenReadabilityWorldFrame;
}

namespace HIKARI::MODELRENDERER {

enum class ModelRendererFrameKind {
    None,
    MainView,
    ReflectionProbeCapture,
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

// 一枚のキャプチャに並べて描く視点の一つ：そのカメラで、像の texel の矩形だけへ描く。
struct SecondarySceneCaptureSubView {
    Camera3D camera{};
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    // 描き直さず前のフレームの絵を残してよい（KeepPrevious）。前の絵が無ければ描く。
    bool keepPrevious = false;
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
void RenderOpaqueForReflectionProbeCapture(const GFX::Context& graphics, const Camera3D& camera,
                                           const SceneEnvironment& environment,
                                           const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t width,
                                           uint32_t height);
bool RenderSecondarySceneCapture(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                 const SceneEnvironment& environment,
                                 const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t captureSlot,
                                 uint32_t width, uint32_t height, bool renderTransparent,
                                 RenderTarget2D* target = nullptr, bool prepareFrame = true, bool subView = false);
bool RenderSecondarySceneCaptureTarget(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                       const SceneEnvironment& environment,
                                       const RENDER3D::LIGHTING::SceneLightingSet& lighting, SkyManager& skyManager,
                                       uint64_t worldIdentity, uint32_t captureSlot, uint32_t width, uint32_t height,
                                       bool renderSky, bool renderTransparent,
                                       const GameProjectSceneCaptureStyleDesc& style,
                                       SecondarySceneCaptureOutput& outOutput,
                                       const std::vector<SecondarySceneCaptureSubView>* subViews = nullptr,
                                       const RENDER3D::ScreenReadabilityWorldFrame* unstyledSurfaces = nullptr);
void ResetSecondarySceneCaptureTarget(uint32_t captureSlot) noexcept;
void ResetSecondarySceneCaptureTarget() noexcept;
const ModelRendererDebugStats& GetDebugStats();
RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache();

} // namespace HIKARI::MODELRENDERER
