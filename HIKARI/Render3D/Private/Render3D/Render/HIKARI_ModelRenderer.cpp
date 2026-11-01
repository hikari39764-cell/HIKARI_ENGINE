#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Targets/HIKARI_SurfaceTextureTargetRing.h"
#include "Render3D/Render/HIKARI_SceneCaptureStylePass.h"
#include "HIKARI/Project/UiCanvasTypes.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"
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
    // 公開した一枚に絵が入っている並べた視点の矩形（x, y, 幅, 高さ）と、その一枚の資源。KeepPrevious の視点は
    // 同じ一枚の同じ矩形に前の絵がある時だけ描かずに残す。
    std::vector<std::array<uint32_t, 4>> shownRects;
    const void* shownDisplay = nullptr;
    void Reset() noexcept {
        display.Reset();
        SurfaceTextureTargetRing::Reset();
        shownRects.clear();
        shownDisplay = nullptr;
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

// 描き方を受けない面（Unstyled の結び付け）の四隅を、描き直す視点ごとにキャプチャの texel へ写す。視点の矩形で切った
// 外接矩形と、面の上の奥行きの式（透視は 1 / 視線方向の距離、正投影は距離。平らな面では texel の一次式）を求め、
// 大きく写る順に描き方の定数に収まる数だけ残す。カメラの後ろへ回る隅のある面はその視点では使わない（普段どおり描く）。
struct UnstyledView {
    const Camera3D* camera = nullptr;
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};
RENDER3D::SceneCaptureUnstyledRegions BuildUnstyledRegions(const RENDER3D::ScreenReadabilityWorldFrame& surfaces,
                                                           const std::vector<UnstyledView>& views) {
    struct Candidate {
        RENDER3D::SceneCaptureUnstyledRegion region{};
        float area = 0.0f;
    };
    std::vector<Candidate> candidates;
    const uint32_t surfaceCount = (std::min)(surfaces.count, RENDER3D::kScreenReadabilityCandidateCount);
    for (const UnstyledView& view : views) {
        const MATH::Mat4 viewProjection = view.camera->GetViewProj();
        const bool orthographic = view.camera->IsOrthographic();
        const float nearZ = view.camera->GetNearZ(), farZ = view.camera->GetFarZ();
        for (uint32_t index = 0u; index < surfaceCount; ++index) {
            std::array<MATH::Vec3, 4> corners{};   // texel の x, y と奥行きの値
            bool valid = true;
            for (uint32_t corner = 0u; corner < 4u && valid; ++corner) {
                const auto& p = surfaces.regions[index][corner];
                const auto clip = viewProjection.TransformPoint({p.x, p.y, p.z, 1.0f});
                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                    clip.w <= .0001f) {
                    valid = false;
                    break;
                }
                const float ndcX = clip.x / clip.w, ndcY = clip.y / clip.w;
                const float value = orthographic ? nearZ + clip.z / clip.w * (farZ - nearZ) : 1.0f / clip.w;
                corners[corner] = {view.x + (ndcX * .5f + .5f) * view.width, view.y + (.5f - ndcY * .5f) * view.height, value};
            }
            if (!valid)
                continue;
            RENDER3D::SceneCaptureUnstyledRegion region{};
            region.left = (std::max)(view.x, (std::min)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}));
            region.right = (std::min)(view.x + view.width, (std::max)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}));
            region.top = (std::max)(view.y, (std::min)({corners[0].y, corners[1].y, corners[2].y, corners[3].y}));
            region.bottom = (std::min)(view.y + view.height, (std::max)({corners[0].y, corners[1].y, corners[2].y, corners[3].y}));
            if (region.right - region.left < 1.0f || region.bottom - region.top < 1.0f)
                continue;
            const float area = (region.right - region.left) * (region.bottom - region.top);
            // 三つの隅を通る一次式 value = a·x + b·y + c（隅 0・1・2 が一直線なら 0・2・3 で）。
            const auto solve = [&region](const MATH::Vec3& p0, const MATH::Vec3& p1, const MATH::Vec3& p2) {
                const float det = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
                if (std::abs(det) < 1e-4f)
                    return false;
                region.a = ((p1.z - p0.z) * (p2.y - p0.y) - (p2.z - p0.z) * (p1.y - p0.y)) / det;
                region.b = ((p2.z - p0.z) * (p1.x - p0.x) - (p1.z - p0.z) * (p2.x - p0.x)) / det;
                region.c = p0.z - region.a * p0.x - region.b * p0.y;
                return true;
            };
            if (!solve(corners[0], corners[1], corners[2]) && !solve(corners[0], corners[2], corners[3]))
                continue;
            candidates.push_back({region, area});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) { return l.area > r.area; });
    RENDER3D::SceneCaptureUnstyledRegions out{};
    for (const Candidate& candidate : candidates) {
        if (out.count == RENDER3D::kSceneCaptureUnstyledRegionCount)
            break;
        out.regions[out.count++] = candidate.region;
    }
    return out;
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
                                 uint32_t width, uint32_t height, bool renderTransparent, RenderTarget2D* target,
                                 bool prepareFrame) {
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
                                                     renderTransparent, target ? &captureResources : nullptr,
                                                     prepareFrame);
}

bool RenderSecondarySceneCaptureTarget(const GFX::Context& graphics, const RENDER3D::RenderViewContext& view,
                                       const SceneEnvironment& environment,
                                       const RENDER3D::LIGHTING::SceneLightingSet& lighting, SkyManager& skyManager,
                                       uint64_t worldIdentity, uint32_t captureSlot, uint32_t width, uint32_t height,
                                       bool renderSky, bool renderTransparent,
                                       const GameProjectSceneCaptureStyleDesc& style,
                                       SecondarySceneCaptureOutput& outOutput,
                                       const std::vector<SecondarySceneCaptureSubView>* subViews,
                                       const RENDER3D::ScreenReadabilityWorldFrame* unstyledSurfaces) {
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

    // 公開用の一枚（描画先の環とは別の slot 番号で登録する）。前のフレームの絵を残せるかを決めるため、描く前に用意する。
    constexpr uint32_t displaySlotBase = kGameProjectSceneCaptureMaximumActiveCount + kGameProjectUiCanvasMaximumCount;
    if (!state.display.Ensure(graphics, worldIdentity, displaySlotBase + captureSlot, width, height,
                              kSecondaryCaptureFormat, false)) {
        return false;
    }
    RenderTarget2D& display = *state.display.targets[0];
    // 並べた視点のうち描かずに残す視点：KeepPrevious の印があり、同じ一枚の同じ矩形に前のフレームの絵がある視点。
    const bool sameDisplay = state.shownDisplay == static_cast<const void*>(display.GetResource());
    std::vector<bool> keep(subViews != nullptr ? subViews->size() : 0u, false);
    bool anyKept = false;
    for (size_t index = 0; index < keep.size(); ++index) {
        const SecondarySceneCaptureSubView& sub = (*subViews)[index];
        const std::array<uint32_t, 4> rect{sub.x, sub.y, sub.width, sub.height};
        keep[index] = sub.keepPrevious && sameDisplay &&
                      std::find(state.shownRects.begin(), state.shownRects.end(), rect) != state.shownRects.end();
        anyKept = anyKept || keep[index];
    }

    RenderTarget2D& target = *state.targets[state.writeIndex];
    target.BeginCapture(0.015f, 0.02f, 0.035f, 1.0f, 1.0f);
    bool rendered = true;
    if (subViews == nullptr) {
        if (renderSky) {
            SKYRENDERER::Render(graphics, view.cameraFrame->camera, environment, skyManager);
            (void)RENDER3D::PANORAMA_SKY::RenderPanoramaSkyLayer(graphics, view.cameraFrame->camera, environment.sky,
                                                                 skyManager);
            target.Rebind();
        }
        rendered = RenderSecondarySceneCapture(graphics, view, environment, lighting, captureSlot, width, height,
                                               renderTransparent, &target);
    } else {
        // 並べた視点：消去は全面に一度、視点ごとに描く範囲を矩形へ絞って同じ深度へ描く。カメラ定数の画面の大きさは像全体
        // （深度や合成を texel の位置で読む処理が像全体の座標を使う）。
        // 場景の上載せ・材質・粒子の準備は最初の視点だけで行い、二つ目からは視点ごとの選別と描画だけにする。
        bool prepareFrame = true;
        for (size_t index = 0; index < subViews->size(); ++index) {
            if (keep[index]) {
                continue;
            }
            const SecondarySceneCaptureSubView& sub = (*subViews)[index];
            RENDER3D::ResolvedCameraFrame subCamera = *view.cameraFrame;
            subCamera.camera = sub.camera;
            RENDER3D::RenderViewContext subContext = view;
            subContext.cameraFrame = &subCamera;
            target.SetViewport(sub.x, sub.y, sub.width, sub.height);
            target.Rebind();
            if (renderSky) {
                SKYRENDERER::Render(graphics, sub.camera, environment, skyManager);
                (void)RENDER3D::PANORAMA_SKY::RenderPanoramaSkyLayer(graphics, sub.camera, environment.sky, skyManager);
                target.Rebind();
            }
            const bool viewRendered = RenderSecondarySceneCapture(graphics, subContext, environment, lighting, captureSlot,
                                                                  width, height, renderTransparent, &target, prepareFrame);
            rendered = viewRendered && rendered;
            // 準備に失敗したフレームは次の視点で準備し直す。
            prepareFrame = prepareFrame && !viewRendered;
        }
        target.ResetViewport();
        target.Rebind();
    }
    target.EndCapture();
    (void)POST::PostSystem::RebindCurrentRenderTarget();
    // Sky/clear is already a valid camera frame. Geometry residency and
    // backend preparation are allowed to recover asynchronously without
    // withholding the texture from the screen material for the complete
    // frame. A capture with no sky still reports the geometry failure.
    if (!rendered && !renderSky) {
        return false;
    }

    // 残す視点があれば、描き方と写しは描き直した視点の矩形の中だけにする（残す矩形は前の絵のまま）。
    std::vector<D3D12_RECT> drawnRects;
    if (anyKept) {
        for (size_t index = 0; index < keep.size(); ++index) {
            if (keep[index]) {
                continue;
            }
            const SecondarySceneCaptureSubView& sub = (*subViews)[index];
            drawnRects.push_back({static_cast<LONG>(sub.x), static_cast<LONG>(sub.y), static_cast<LONG>(sub.x + sub.width),
                                  static_cast<LONG>(sub.y + sub.height)});
        }
    }
    static const auto styleTimer = CPU_PROFILE::RegisterDynamicPass("Render.SceneCaptures.Style");
    const CPU_PROFILE::ScopedDynamicCpuTimer styleScope(styleTimer);
    if (style.style != GameProjectSceneCaptureStyle::None) {
        // 描き方を受けない面（Unstyled）：描き直した視点に写る所を texel の矩形と奥行きの式にする。
        RENDER3D::SceneCaptureUnstyledRegions unstyled{};
        if (unstyledSurfaces != nullptr && unstyledSurfaces->count > 0u) {
            std::vector<UnstyledView> views;
            if (subViews == nullptr) {
                views.push_back({&view.cameraFrame->camera, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
            } else {
                for (size_t index = 0; index < subViews->size(); ++index) {
                    if (keep[index]) {
                        continue;
                    }
                    const SecondarySceneCaptureSubView& sub = (*subViews)[index];
                    views.push_back({&sub.camera, static_cast<float>(sub.x), static_cast<float>(sub.y),
                                     static_cast<float>(sub.width), static_cast<float>(sub.height)});
                }
            }
            unstyled = BuildUnstyledRegions(*unstyledSurfaces, views);
        }
        // 深度の効果（版 4）か描き方を受けない面がある時だけ、キャプチャの深度を読める状態にして渡す。
        RENDER3D::SceneCaptureStyleDepth depth{};
        const bool readDepth = (style.depthFocus > 0.0f || unstyled.count > 0u) && target.BeginDepthShaderRead();
        if (readDepth) {
            // 並べた視点は深度の範囲と投影の種類がそろっている（SetViews の検査）ので、最初の視点で読む。
            const Camera3D& camera =
                subViews != nullptr && !subViews->empty() ? subViews->front().camera : view.cameraFrame->camera;
            depth.srv = target.GetDepthSrvGpu();
            depth.nearZ = camera.GetNearZ();
            depth.farZ = camera.GetFarZ();
            depth.orthographic = camera.IsOrthographic();
        }
        const bool drawn = RENDER3D::DrawSceneCaptureStyle(graphics, target, display, style, depth,
                                                           anyKept ? &drawnRects : nullptr, &unstyled);
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
        if (!anyKept) {
            graphics.cmdList->CopyResource(display.GetResource(), target.GetResource());
        } else {
            for (const D3D12_RECT& rect : drawnRects) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = display.GetResource();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION source = destination;
                source.pResource = target.GetResource();
                const D3D12_BOX box{static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), 0u, static_cast<UINT>(rect.right),
                                    static_cast<UINT>(rect.bottom), 1u};
                graphics.cmdList->CopyTextureRegion(&destination, box.left, box.top, 0u, &source, &box);
            }
        }
        target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        display.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // 公開した一枚に絵が入った矩形を覚える（並べた視点は残した視点も含めて全部に絵がある）。
    state.shownDisplay = display.GetResource();
    state.shownRects.clear();
    if (subViews != nullptr) {
        for (const SecondarySceneCaptureSubView& sub : *subViews) {
            state.shownRects.push_back({sub.x, sub.y, sub.width, sub.height});
        }
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
