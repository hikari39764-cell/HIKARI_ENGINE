#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_SurfaceTextureTargetRing.h"
#include "Render3D/Render/HIKARI_SceneCaptureStylePass.h"
#include "HIKARI/Project/UiCanvasTypes.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::MODELRENDERER {

namespace {
ModelRendererDebugStats gDebugStats{};
RENDER3D::RUNTIME::RenderModelCache gRenderModelCache{};
constexpr DXGI_FORMAT kSecondaryCaptureFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

// 描画先は順送りの環。公開する画面（材質へ貼る texture）は捕捉ごとに固定の一枚で、描き終えた画面を
// 毎回そこへ描き方を掛けて描くか写す。公開の texture が毎フレーム替わると貼った材質の版が進み、
// 影の cache・GPU scene・光追の加速構造まで毎フレーム作り直しになる。
struct SecondaryCaptureTargetState : SurfaceTextureTargetRing {
    SurfaceTextureTargets<1> display;
    void Reset() noexcept {
        display.Reset();
        SurfaceTextureTargetRing::Reset();
    }
};

std::array<SecondaryCaptureTargetState, MESHRENDERER::kSecondarySceneCaptureMaximumCount>& SecondaryCaptureTargets() {
    static std::array<SecondaryCaptureTargetState, MESHRENDERER::kSecondarySceneCaptureMaximumCount> states{};
    return states;
}

void SyncRenderModelCacheStats() {
    const RENDER3D::RUNTIME::RenderModelCache::Stats& cacheStats = gRenderModelCache.GetStats();
    gDebugStats.cache.renderModelCacheRequestCount = cacheStats.requestCount;
    gDebugStats.cache.renderModelCacheHitCount = cacheStats.hitCount;
    gDebugStats.cache.renderModelCacheMissCount = cacheStats.missCount;
    gDebugStats.cache.renderModelCacheInvalidCount = cacheStats.invalidModelCount;
    gDebugStats.cache.renderModelCachedModelCount = cacheStats.cachedModelCount;
    gDebugStats.cache.renderModelCachedSurfaceCount = cacheStats.cachedSurfaceCount;
}
} // namespace

const char* ToString(ModelRendererFrameKind kind) {
    switch (kind) {
    case ModelRendererFrameKind::MainView: return "MainView";
    case ModelRendererFrameKind::ReflectionProbeCapture: return "ReflectionProbeCapture";
    case ModelRendererFrameKind::LightProbeCapture: return "LightProbeCapture";
    case ModelRendererFrameKind::SecondarySceneCapture: return "SecondarySceneCapture";
    case ModelRendererFrameKind::None:
    default: return "None";
    }
}

void ResetModelRendererFrameStats() { gDebugStats.frame = {}; }

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
    for (SecondaryCaptureTargetState& state : SecondaryCaptureTargets()) {
        state.Reset();
    }
    MESHRENDERER::Reset();
    RENDER3D::ResetSceneCaptureStylePass();
    SHADOW::Reset();
}

MESHRENDERER::MeshRendererFrameResult RenderAll(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                                const SceneEnvironment& environment,
                                                const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                RenderDebugView debugView) {
    if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
        return {};
    }

    const Camera3D& camera = view.cameraFrame->camera;
    BeginModelRendererFrame(ModelRendererFrameKind::MainView);
    auto* sprites = MESHRENDERER::PrepareWorldSpritesForView(graphics, view);
    SHADOW::BeginFrame(graphics, environment, camera, sprites);
    SHADOW::RenderDirectionalShadowMap(graphics);
    SHADOW::RenderLocalLightShadows(
        graphics, lighting, camera, MESHRENDERER::GetActiveGpuDrivenSceneSource(),
        SHADOW::ResolveLocalShadowBudget(RENDER3D::GetRenderQualitySettings().localShadowQuality), sprites);
    return MESHRENDERER::RenderAll(graphics, view, environment, lighting, debugView);
}

void RenderOpaqueForReflectionProbeCapture(const GFX::Context& graphics, const Camera3D& camera,
                                           const SceneEnvironment& environment,
                                           const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t width,
                                           uint32_t height, ModelRendererFrameKind kind) {
    BeginModelRendererFrame(kind);
    (void)RENDER3D::PIPELINE::RenderMeshCaptureOpaqueFrame(graphics, camera, environment, lighting, width, height);
}

bool RenderSecondarySceneCapture(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                 const SceneEnvironment& environment,
                                 const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t captureSlot,
                                 uint32_t width, uint32_t height, bool renderTransparent, RenderTarget2D* target) {
    BeginModelRendererFrame(ModelRendererFrameKind::SecondarySceneCapture);
    MESHRENDERER::MeshPassResources captureResources;
    if (target) {
        captureResources.compositionDepth = target->GetReadOnlyDsvHandle();
        captureResources.compositionTarget.userData = target;
        captureResources.compositionTarget.rebind = [](void* data) {
            static_cast<RenderTarget2D*>(data)->Rebind();
            return true;
        };
        captureResources.compositionTarget.beginDepthRead = [](void* data) {
            return static_cast<RenderTarget2D*>(data)->BeginDepthRead();
        };
        captureResources.compositionTarget.endDepthRead = [](void* data) {
            static_cast<RenderTarget2D*>(data)->EndDepthRead();
        };
        captureResources.sceneDepth.resource = target->GetDepthResource();
        captureResources.sceneDepth.srv = target->GetDepthSrvGpu();
        captureResources.sceneDepth.width = width;
        captureResources.sceneDepth.height = height;
        captureResources.sceneDepth.valid = target->GetDepthResource() != nullptr;
    }
    return MESHRENDERER::RenderSecondarySceneCapture(graphics, view, environment, lighting, captureSlot, width, height,
                                                     renderTransparent, target ? &captureResources : nullptr);
}

bool RenderSecondarySceneCaptureTarget(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                       const SceneEnvironment& environment,
                                       const RENDER3D::LIGHTING::SceneLightingSet& lighting, ModelManager& modelManager,
                                       SkyManager& skyManager, uint64_t worldIdentity, uint32_t captureSlot,
                                       uint32_t width, uint32_t height, bool renderSky, bool renderTransparent,
                                       const GameProjectSceneCaptureStyleDesc& style,
                                       SecondarySceneCaptureOutput& outOutput) {
    outOutput = {};
    if (captureSlot >= MESHRENDERER::kSecondarySceneCaptureMaximumCount) {
        return false;
    }
    SecondaryCaptureTargetState& state = SecondaryCaptureTargets()[captureSlot];
    if (view.cameraFrame == nullptr || !view.cameraFrame->valid ||
        !state.Ensure(graphics, worldIdentity, captureSlot, width, height, kSecondaryCaptureFormat, true,
                      {0.015f, 0.02f, 0.035f, 1.0f}, true)) {
        return false;
    }

    RenderTarget2D& target = *state.targets[state.writeIndex];
    target.BeginCapture(0.015f, 0.02f, 0.035f, 1.0f, 1.0f);
    if (renderSky) {
        SKYRENDERER::Render(graphics, view.cameraFrame->camera, environment, modelManager, skyManager);
        (void)RENDER3D::PANORAMA_SKY::RenderPanoramaSkyLayer(graphics, view.cameraFrame->camera, environment.sky,
                                                             skyManager);
        target.Rebind();
    }
    const bool rendered = RenderSecondarySceneCapture(graphics, view, environment, lighting, captureSlot, width, height,
                                                      renderTransparent, &target);
    target.EndCapture();
    (void)POST::PostSystem::RebindCurrentRenderTarget();
    // Sky/clear is already a valid camera frame. Geometry residency and
    // backend preparation are allowed to recover asynchronously without
    // withholding the texture from the screen material for the complete
    // frame. A capture with no sky still reports the geometry failure.
    if (!rendered && !renderSky) {
        return false;
    }

    // 公開用の一枚（描画先の環とは別の slot 番号で登録する）。
    constexpr uint32_t displaySlotBase = kGameProjectSceneCaptureMaximumActiveCount + kGameProjectUiCanvasMaximumCount;
    if (!state.display.Ensure(graphics, worldIdentity, displaySlotBase + captureSlot, width, height,
                              kSecondaryCaptureFormat, false)) {
        return false;
    }
    RenderTarget2D& display = *state.display.targets[0];
    if (style.style != GameProjectSceneCaptureStyle::None) {
        // 深度の効果（版 4）を使う時だけ、キャプチャの深度を読める状態にして渡す。
        RENDER3D::SceneCaptureStyleDepth depth{};
        const bool readDepth = style.depthFocus > 0.0f && target.BeginDepthShaderRead();
        if (readDepth) {
            const Camera3D& camera = view.cameraFrame->camera;
            depth.srv = target.GetDepthSrvGpu();
            depth.nearZ = camera.GetNearZ();
            depth.farZ = camera.GetFarZ();
            depth.orthographic = camera.IsOrthographic();
        }
        const bool drawn = RENDER3D::DrawSceneCaptureStyle(graphics, target, display, style, depth);
        if (readDepth) {
            target.EndDepthShaderRead();
        }
        (void)POST::PostSystem::RebindCurrentRenderTarget();
        if (!drawn) {
            return false;
        }
    } else {
        target.TransitionColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
        display.TransitionColor(D3D12_RESOURCE_STATE_COPY_DEST);
        graphics.cmdList->CopyResource(display.GetResource(), target.GetResource());
        target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        display.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    NUMERIC::AdvanceNonZero(state.outputRevision);
    outOutput.texture = state.display.textures[0];
    outOutput.width = width;
    outOutput.height = height;
    outOutput.outputRevision = state.outputRevision;
    outOutput.ready = true;
    state.writeIndex = (state.writeIndex + 1u) % static_cast<uint32_t>(state.targets.size());
    return true;
}

void ResetSecondarySceneCaptureTarget(uint32_t captureSlot) noexcept {
    try {
        // World/Scene rebuild may run after this frame has already
        // recorded secondary-view commands. The triple-buffer targets
        // retire their native resources through the frame fence, but the
        // per-slot secondary mesh-view buffers/descriptors are renderer
        // lifetime state and must not be destroyed at this boundary.
        // MESHRENDERER::Reset() remains their sole shutdown/device-reset
        // owner.
        if (captureSlot < MESHRENDERER::kSecondarySceneCaptureMaximumCount) {
            SecondaryCaptureTargets()[captureSlot].Reset();
        }
    } catch (...) {
    }
}

void ResetSecondarySceneCaptureTarget() noexcept {
    for (uint32_t captureSlot = 0u; captureSlot < MESHRENDERER::kSecondarySceneCaptureMaximumCount; ++captureSlot) {
        ResetSecondarySceneCaptureTarget(captureSlot);
    }
}

const ModelRendererDebugStats& GetDebugStats() {
    SyncRenderModelCacheStats();
    return gDebugStats;
}

RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache() { return gRenderModelCache; }

} // namespace HIKARI::MODELRENDERER
