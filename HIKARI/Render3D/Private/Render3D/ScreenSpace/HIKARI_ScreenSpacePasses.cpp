#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

namespace {
ScreenSpaceRuntimeState gScreenSpaceState{};
DepthVisibilityDebugState gDepthVisibilityDebugState{};

using CpuClock = std::chrono::steady_clock;

float ElapsedMs(CpuClock::time_point start, CpuClock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

float MaxAbsDeltaMat4(const MATH::Mat4& lhs, const MATH::Mat4& rhs) {
    float maxDelta = 0.0f;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            maxDelta = (std::max)(maxDelta, std::fabs(lhs.m[col][row] - rhs.m[col][row]));
        }
    }
    return maxDelta;
}

bool IsDepthPyramidStatsUsable(const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats, uint32_t width,
                               uint32_t height) {
    const HIKARI::RENDER3D::DEPTH::DepthPyramidView& pyramid = stats.depthPyramid;
    return pyramid.valid && pyramid.pyramidSrv.ptr != 0 && pyramid.sourceWidth == width &&
           pyramid.sourceHeight == height && pyramid.width != 0 && pyramid.height != 0 && pyramid.viewProjValid;
}

bool IsHistoryDepthPyramidUsableForCullingView(const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
                                               uint32_t width, uint32_t height, const MATH::Mat4& viewProj) {
    if (!IsDepthPyramidStatsUsable(stats, width, height)) {
        return false;
    }

    constexpr float kHistoryViewProjMaxDelta = 0.0125f;
    return MaxAbsDeltaMat4(stats.depthPyramid.viewProj, viewProj) <= kHistoryViewProjMaxDelta;
}

void ClearFrozenCullingDepthStats(ScreenSpaceRuntimeState& state) {
    state.frozenCullingDepthStats = {};
    state.frozenCullingDepthStatsValid = false;
}

void PublishVisibilityDepthPyramid(ScreenSpaceFrameResult& result, DEPTH::DepthPyramidView view,
                                   DEPTH::DepthPyramidViewKind viewKind, RenderFrameToken publication) {
    if (!publication.IsValid() || !view.valid || view.pyramidSrv.ptr == 0 || view.width == 0 || view.height == 0) {
        return;
    }

    view.viewKind = viewKind;
    view.publication = publication;
    result.hzbBuilt = true;
    result.visibilityDepthPyramid = view;
}

void PublishCurrentSceneDepthPyramid(ScreenSpaceFrameResult& result, DEPTH::DepthPyramidView view,
                                     RenderFrameToken publication) {
    if (!publication.IsValid() || !view.valid || view.sourceKind != DEPTH::DepthPyramidSourceKind::SceneDepth ||
        view.pyramidSrv.ptr == 0 || view.width == 0 || view.height == 0) {
        return;
    }

    view.viewKind = DEPTH::DepthPyramidViewKind::CurrentFrame;
    view.publication = publication;
    result.hzbBuilt = true;
    result.currentSceneDepthPyramid = view;
}

RenderTextureView BuildTextureResourceView(TextureResourceHandle handle) {
    if (!IsTextureResourceValid(handle)) {
        return {};
    }
    const RenderResourceRecord* record = GetTextureResourceRecord(handle);
    if (record == nullptr) {
        return {};
    }
    const D3D12_GPU_DESCRIPTOR_HANDLE srv = GetTextureResourceSrvGpuHandle(handle);
    return {record->GetResource(),
            srv,
            record->desc.format,
            static_cast<uint32_t>(record->desc.width),
            record->desc.height,
            record->desc.mipLevels,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            record->GetResource() != nullptr && srv.ptr != 0};
}

void BeginDepthVisibilityDebugFrame(uint32_t width, uint32_t height, bool historyReady, bool historyMatched) {
    gDepthVisibilityDebugState = {};
    gDepthVisibilityDebugState.width = width;
    gDepthVisibilityDebugState.height = height;
    gDepthVisibilityDebugState.historyReady = historyReady;
    gDepthVisibilityDebugState.historyMatched = historyMatched;
}

void RecordDepthVisibilitySource(DepthVisibilitySource source) {
    gDepthVisibilityDebugState.visibilitySource = source;
    gDepthVisibilityDebugState.visibilityUsedHzb =
        source != DepthVisibilitySource::None && source != DepthVisibilitySource::NoHzb;
    gDepthVisibilityDebugState.visibilityWithoutHzb = source == DepthVisibilitySource::NoHzb;
    if (gDepthVisibilityDebugState.visibilityUsedHzb) {
        gDepthVisibilityDebugState.depthPyramidBuilt = true;
        gDepthVisibilityDebugState.latestPyramidSource = source;
    }
}

void RecordDepthPyramidSource(DepthVisibilitySource source) {
    if (source == DepthVisibilitySource::None || source == DepthVisibilitySource::NoHzb) {
        return;
    }
    gDepthVisibilityDebugState.depthPyramidBuilt = true;
    gDepthVisibilityDebugState.latestPyramidSource = source;
}

void RecordDepthVisibilityWithoutHzb() { RecordDepthVisibilitySource(DepthVisibilitySource::NoHzb); }

// History HZB が使えないフレームでも遮蔽剔除を丸ごと失わないよう、
// occluder のみの depth prepass から当該フレームの depth pyramid を構築し、
// GPU-driven visibility を確定する。pyramid が成立したら true を返す。
bool BuildDepthVisibilityFromOccluderPrepass(ScreenSpaceRuntimeState& state,
                                             const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                             const MESHRENDERER::CameraCB& cullingCameraCb,
                                             ScreenSpaceFrameResult& result) {
    const bool frozen = MESHRENDERER::IsGpuDrivenCullingCameraFrozen();
    const auto& depthCamera = frozen ? cullingCameraCb : *MESHRENDERER::GetCameraConstants();
    bool depthWritten = false;
    if (MESHRENDERER::HasDepthPrepassWork()) {
        const D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv =
            state.depthVisibility.BeginDepthPrepass(*context.graphics, context.cmd, context.width, context.height);
        depthWritten =
            visibilityDsv.ptr != 0 && MESHRENDERER::RenderDepthPrepass(*context.graphics, visibilityDsv, frozen);
    }
    result.depthPrepassWritten = depthWritten;
    state.depthVisibility.RecordDepthPrepass(depthWritten);
    gDepthVisibilityDebugState.depthPrepassWritten = depthWritten;
    if (depthWritten) {
        result.hzbBuilt = state.depthVisibility.BuildDepthPyramidFromVisibilityPrepass(
            *context.graphics, context.cmd, context.width, context.height, depthCamera.viewProj,
            !frozen && context.jitteredViewProj);
    }

    context.renderTargetAccess.Rebind();
    const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& currentDepthStats = state.depthVisibility.GetStats();
    // ResetFrame は旧 pyramid を保持する。今回の build 失敗を成功扱いしない。
    state.depthVisibilityValid =
        depthWritten && result.hzbBuilt &&
        std::memcmp(&currentDepthStats.depthPyramid.viewProj, &depthCamera.viewProj, sizeof(MATH::Mat4)) == 0 &&
        IsDepthPyramidStatsUsable(currentDepthStats, context.width, context.height);
    state.depthVisibilityViewProjValid = state.depthVisibilityValid;
    state.depthVisibilityViewProj = currentDepthStats.depthPyramid.viewProj;
    if (!state.depthVisibilityValid) {
        RecordDepthVisibilityWithoutHzb();
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(*context.graphics);
        return false;
    }

    RecordDepthPyramidSource(DepthVisibilitySource::CurrentPrepass);
    PublishVisibilityDepthPyramid(
        result, currentDepthStats.depthPyramid,
        frozen ? DEPTH::DepthPyramidViewKind::Frozen : DEPTH::DepthPyramidViewKind::CurrentFrame, context.frame);
    RecordDepthVisibilitySource(DepthVisibilitySource::CurrentPrepass);
    (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(*context.graphics, currentDepthStats);
    return true;
}
} // namespace

ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState() { return gScreenSpaceState; }

const DepthVisibilityDebugState& GetDepthVisibilityDebugState() { return gDepthVisibilityDebugState; }

const char* ToString(DepthVisibilitySource source) {
    switch (source) {
    case DepthVisibilitySource::History: return "History";
    case DepthVisibilitySource::Frozen: return "Frozen";
    case DepthVisibilitySource::CurrentPrepass: return "CurrentPrepass";
    case DepthVisibilitySource::CurrentSceneDepth: return "CurrentSceneDepth";
    case DepthVisibilitySource::NoHzb: return "NoHzb";
    case DepthVisibilitySource::None:
    default: return "None";
    }
}

void ReleaseScreenSpaceRuntimeState() {
    gScreenSpaceState.surfaceBuffer.Release();
    gScreenSpaceState.reflectionBuffer.Release();
    gScreenSpaceState.ssaoRenderer.Release();
    gScreenSpaceState.rayTracedShadowRenderer.Release();
    gScreenSpaceState.depthVisibility.Release();
    RENDER3D::ReleaseTextureResource(gScreenSpaceState.fallbackAoTextureResource);
    gScreenSpaceState.fallbackAoTextureResource = {};
    gScreenSpaceState.fallbackAoTextureHandle = -1;
    gScreenSpaceState.depthVisibilityValid = false;
    gScreenSpaceState.depthVisibilityViewProjValid = false;
    gScreenSpaceState.depthVisibilityBuildAllowedThisFrame = true;
    ClearFrozenCullingDepthStats(gScreenSpaceState);
    gScreenSpaceState.geometryValid = false;
    gScreenSpaceState.ssaoValid = false;
}

bool EnsureScreenSpaceFallbacks(ScreenSpaceRuntimeState& state) {
    if (RENDER3D::IsTextureResourceValid(state.fallbackAoTextureResource)) {
        state.fallbackAoTextureHandle = RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
        return state.fallbackAoTextureHandle >= 0;
    }

    state.fallbackAoTextureHandle = -1;
    state.fallbackAoTextureResource = RENDER3D::CreateSolidColorTextureResource(
        "screen_space/fallback_ao", 0xffffffffu, RENDER3D::TextureResourceColorSpace::Linear);
    state.fallbackAoTextureHandle = RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
    return RENDER3D::IsTextureResourceValid(state.fallbackAoTextureResource) && state.fallbackAoTextureHandle >= 0;
}

ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(ScreenSpaceRuntimeState& state,
                                                           const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                                           const MESHRENDERER::CameraCB& renderCameraCb,
                                                           const MESHRENDERER::CameraCB& cullingCameraCb,
                                                           const SceneEnvironment& environment,
                                                           const ScreenSpaceSurfaceRequirements& requirements) {
    ScreenSpaceFrameResult result{};
    if (context.graphics == nullptr) {
        return result;
    }
    if (!requirements.requireFeatureMask) {
        (void)state.surfaceBuffer.SetFeatureMaskEnabled(*context.graphics, false);
    }
    DEPTH::UpdateDepthPyramidContext(*context.graphics);
    const bool fallbackReady = EnsureScreenSpaceFallbacks(state);
    if (fallbackReady) {
        result.fallbackAoTextureResource = state.fallbackAoTextureResource;
        result.ambientOcclusion = BuildTextureResourceView(state.fallbackAoTextureResource);
    }
    result.fallbackAoTextureHandle = state.fallbackAoTextureHandle;
    const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats historyDepthStats = state.depthVisibility.GetStats();
    const bool historyDepthPyramidReady = IsDepthPyramidStatsUsable(historyDepthStats, context.width, context.height);
    const bool historyDepthPyramidMatchesCullingView = IsHistoryDepthPyramidUsableForCullingView(
        historyDepthStats, context.width, context.height, cullingCameraCb.viewProj);
    state.depthVisibility.ResetFrame();
    BeginDepthVisibilityDebugFrame(context.width, context.height, historyDepthPyramidReady,
                                   historyDepthPyramidMatchesCullingView);
    BeginSsaoDebugFrame(context.width, context.height, environment.ambientOcclusion);

    if (context.graphics == nullptr || context.cmd == nullptr) {
        state.depthVisibilityValid = false;
        state.depthVisibilityViewProjValid = false;
        state.ssaoValid = false;
        return result;
    }

    // Screen-space pass は forward lighting の前に必要な texture を生成する。
    GFX::PIX::ScopedGpuEvent pixScreenSpace(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PreLighting");

    const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);

    const bool ssrNeedsSurface = environment.screenSpaceReflections.enabled &&
                                 environment.screenSpaceReflections.quality != ScreenSpaceReflectionQuality::Off;
    const bool geometryAuxRequired = ssrNeedsSurface || requirements.requirePackedSurface ||
                                     (ssaoMode != SsaoMode::Off && ssaoMode != SsaoMode::Balanced);
    const bool meshletCurrentDepth = geometryAuxRequired &&
                                     GetRenderQualitySettings().geometryPipeline == GeometryPipelineMode::MeshShader &&
                                     !MESHRENDERER::IsGpuDrivenCullingCameraFrozen();

    const CpuClock::time_point depthVisibilityStart = CpuClock::now();
    const bool depthVisibilityAllowedThisFrame = true;
    state.depthVisibilityBuildAllowedThisFrame = depthVisibilityAllowedThisFrame;
    const bool cullingCameraFrozen = MESHRENDERER::IsGpuDrivenCullingCameraFrozen();
    if (!cullingCameraFrozen) {
        ClearFrozenCullingDepthStats(state);
    }

    if (meshletCurrentDepth) {
        // Aux の current depth から AS が主描画の meshlet 遮蔽を判定する。
        // CS の候補作成は一度だけ。旧 depth による誤剔除を持ち越さない。
        state.depthVisibilityValid = false;
        RecordDepthVisibilityWithoutHzb();
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(*context.graphics);
    } else if (cullingCameraFrozen) {
        // Frozen は専用 prepass の厳密な camera/depth 対を保持する。
        // 通常 history の近似一致や jitter 差を frozen として承認しない。
        const bool frozenDepthReady =
            state.frozenCullingDepthStatsValid &&
            IsDepthPyramidStatsUsable(state.frozenCullingDepthStats, context.width, context.height) &&
            std::memcmp(&state.frozenCullingDepthStats.depthPyramid.viewProj, &cullingCameraCb.viewProj,
                        sizeof(MATH::Mat4)) == 0;
        if (frozenDepthReady) {
            GFX::PIX::ScopedGpuEvent pixFrozen(context.cmd, GFX::PIX::kColorUpload,
                                               "GpuDepthVisibility.UseFrozenDepthPyramid");
            state.depthVisibilityValid = true;
            state.depthVisibilityViewProjValid = true;
            state.depthVisibilityViewProj = state.frozenCullingDepthStats.depthPyramid.viewProj;
            PublishVisibilityDepthPyramid(result, state.frozenCullingDepthStats.depthPyramid,
                                          DEPTH::DepthPyramidViewKind::Frozen, context.frame);
            RecordDepthVisibilitySource(DepthVisibilitySource::Frozen);
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(*context.graphics, state.frozenCullingDepthStats);
        } else if (BuildDepthVisibilityFromOccluderPrepass(state, context, cullingCameraCb, result)) {
            state.frozenCullingDepthStats = state.depthVisibility.GetStats();
            state.frozenCullingDepthStatsValid = true;
            RecordDepthVisibilitySource(DepthVisibilitySource::Frozen);
        } else {
            ClearFrozenCullingDepthStats(state);
        }
    } else if (depthVisibilityAllowedThisFrame && historyDepthPyramidReady && historyDepthPyramidMatchesCullingView) {
        GFX::PIX::ScopedGpuEvent pixHistory(context.cmd, GFX::PIX::kColorUpload,
                                            "GpuDepthVisibility.UseHistoryDepthPyramid");
        state.depthVisibilityValid = true;
        state.depthVisibilityViewProjValid = true;
        state.depthVisibilityViewProj = historyDepthStats.depthPyramid.viewProj;
        PublishVisibilityDepthPyramid(result, historyDepthStats.depthPyramid, DEPTH::DepthPyramidViewKind::History,
                                      context.frame);
        RecordDepthVisibilitySource(DepthVisibilitySource::History);
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(*context.graphics, historyDepthStats);
    } else if (historyDepthPyramidReady) {
        // History pyramid は健在だが視点が動いて許容差を超えたフレーム。
        // カメラ移動中は毎フレームここに来るため、occluder prepass の再構築
        // (full depth 描画 + pyramid build) は高くつく。frustum のみに落とし、
        // ピクセル過描画は scene depth prepass に任せる。
        state.depthVisibilityValid = false;
        state.depthVisibilityViewProjValid = false;
        RecordDepthVisibilityWithoutHzb();
        (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth(*context.graphics);
    } else {
        // Pyramid が構造的に無い (初回 / resize 直後)。occluder prepass から
        // 当該フレームの pyramid を作って遮蔽剔除を確保する。
        (void)BuildDepthVisibilityFromOccluderPrepass(state, context, cullingCameraCb, result);
    }

    const float depthVisibilityMs = ElapsedMs(depthVisibilityStart, CpuClock::now());

    // Off 時は GeometryAux も作らない。
    if (ssaoMode == SsaoMode::Off) {
        if (ssrNeedsSurface || requirements.requirePackedSurface) {
            // Continue to the shared surface write; SSAO remains disabled.
        } else {
            state.ssaoRenderer.RecordSkipped(context.width, context.height, environment.ambientOcclusion);
            state.geometryValid = false;
            state.ssaoValid = false;
            return result;
        }
    }

    if (ssaoMode == SsaoMode::Balanced && !ssrNeedsSurface && !requirements.requirePackedSurface) {
        RecordSsaoGeometryAuxDebug(false, depthVisibilityMs, DXGI_FORMAT_UNKNOWN);
        state.geometryValid = false;

        // 正交ビューは、この後の scene prepass が供給する現フレーム AO のみ使用。
        if (!requirements.requireCurrentFrameAo && state.ssaoValid && state.ssaoRenderer.IsValid() &&
            state.ssaoRenderer.GetAoSrv().ptr != 0) {
            result.ssaoRendered = true;
            result.ambientOcclusion = state.ssaoRenderer.GetAoView();
            GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
            RecordSsaoCompositeDebug(0.0f);
        } else {
            state.ssaoValid = false;
            result.ssaoRendered = false;
        }
        return result;
    }

    GFX::PIX::ScopedGpuEvent pixGeometry(context.cmd, GFX::PIX::kColorRender, "GeometryAux");
    GFX::GPU_PROFILE::ScopedGpuTimer gpuGeometry(context.cmd, GFX::GPU_PROFILE::Pass::GeometryAux);
    const CpuClock::time_point geometryStart = CpuClock::now();
    result.geometryAuxWritten = MESHRENDERER::RenderGeometryAuxPass(*context.graphics, state.surfaceBuffer,
                                                                    context.sceneDsv, requirements.requireFeatureMask);
    result.geometryAuxDepthComplete = result.geometryAuxWritten;
    context.renderTargetAccess.Rebind();
    RecordSsaoGeometryAuxDebug(result.geometryAuxWritten, ElapsedMs(geometryStart, CpuClock::now()),
                               state.surfaceBuffer.GetFormat());

    state.geometryValid = result.geometryAuxWritten && state.surfaceBuffer.IsValid();
    if (state.geometryValid) {
        result.packedSurface = state.surfaceBuffer.GetPackedSurfaceView();
        if (requirements.requireFeatureMask && state.surfaceBuffer.HasFeatureMask()) {
            result.surfaceFeatureMask = state.surfaceBuffer.GetFeatureMaskView();
            if (state.surfaceBuffer.HasStylizedOutlineStyle()) {
                result.stylizedOutlineStyle = state.surfaceBuffer.GetStylizedOutlineStyleView();
            }
        }
    }
    if (meshletCurrentDepth && state.geometryValid) {
        (void)BuildCurrentFrameDepthPyramid(state, context, renderCameraCb, result);
    }
    if (!state.geometryValid || !context.sceneDepth.IsSampleable()) {
        state.ssaoValid = false;
        return result;
    }

    if (ssaoMode == SsaoMode::Off) {
        state.ssaoRenderer.RecordSkipped(context.width, context.height, environment.ambientOcclusion);
        state.ssaoValid = false;
        return result;
    }

    if (ssaoMode == SsaoMode::Balanced) {
        // GeometryAux の深度は Sprite の cutout も含む現フレームの入力。
        // 前フレームの AO を貼ると、移動する輪郭に背景の明暗が残る。
        (void)ExecuteBalancedSsaoFromSceneDepth(state, context, renderCameraCb, environment, result);
        return result;
    }

    bool ssaoOk = false;
    if (context.renderTargetAccess.BeginDepthRead()) {
        ssaoOk = state.ssaoRenderer.Render(*context.graphics, context.cmd, state.surfaceBuffer, context.sceneDepth.srv,
                                           renderCameraCb, environment.ambientOcclusion);
        context.renderTargetAccess.EndDepthRead();
        // SSAO は内部 AO RT を複数回 bind するので、lighting pass の前に scene RT へ戻す。
        context.renderTargetAccess.Rebind();
    }

    state.ssaoValid = ssaoOk;
    result.ssaoRendered = ssaoOk;
    if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
        result.ambientOcclusion = state.ssaoRenderer.GetAoView();
        GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
        RecordSsaoCompositeDebug(0.0f);
    }
    return result;
}

bool ExecuteScreenSpacePostOpaquePasses(ScreenSpaceRuntimeState& state,
                                        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                        const MESHRENDERER::CameraCB& cameraCb, const SceneEnvironment& environment,
                                        ScreenSpaceFrameResult& result) {
    if (context.cmd == nullptr) {
        return false;
    }

    const bool depthAvailable = context.sceneDepth.IsSampleable();
    if (!depthAvailable) {
        state.depthVisibilityValid = false;
        state.depthVisibilityViewProjValid = false;
        return context.renderTargetAccess.Rebind();
    }

    const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);
    // 同フレーム経路 (ExecuteBalancedSsaoFromSceneDepth) が成功した場合は
    // temporal 更新を省く。フラグはここで毎フレーム消費する。
    const bool refreshBalancedSsao = ssaoMode == SsaoMode::Balanced && !state.balancedSsaoSameFrame;
    state.balancedSsaoSameFrame = false;

    if (!context.renderTargetAccess.BeginDepthRead()) {
        state.depthVisibilityValid = false;
        state.depthVisibilityViewProjValid = false;
        return false;
    }

    {
        GFX::PIX::ScopedGpuEvent pixPostOpaque(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PostOpaqueTemporal");

        if (state.depthVisibilityBuildAllowedThisFrame && !MESHRENDERER::IsGpuDrivenCullingCameraFrozen()) {
            const auto& auxiliaryDepth = result.visibilityDepthPyramid;
            const bool reuseAuxiliaryDepth =
                result.geometryAuxDepthComplete && auxiliaryDepth.valid && auxiliaryDepth.viewProjValid &&
                auxiliaryDepth.viewKind == DEPTH::DepthPyramidViewKind::CurrentFrame &&
                auxiliaryDepth.sourceKind == DEPTH::DepthPyramidSourceKind::SceneDepth &&
                auxiliaryDepth.publication.Matches(context.frame) && auxiliaryDepth.sourceWidth == context.width &&
                auxiliaryDepth.sourceHeight == context.height &&
                MaxAbsDeltaMat4(auxiliaryDepth.viewProj, cameraCb.viewProj) == 0.0f;
            if (reuseAuxiliaryDepth) {
                result.hzbBuilt = true;
                gDepthVisibilityDebugState.auxiliaryDepthReused = true;
            } else {
                result.hzbBuilt = state.depthVisibility.BuildDepthPyramidFromDepthSrv(
                    *context.graphics, context.cmd, context.width, context.height, context.sceneDepth.srv,
                    cameraCb.viewProj, context.jitteredViewProj);
                if (result.hzbBuilt)
                    ++gDepthVisibilityDebugState.sceneDepthPyramidBuildCount;
            }
            if (result.hzbBuilt) {
                RecordDepthPyramidSource(DepthVisibilitySource::CurrentSceneDepth);
                PublishCurrentSceneDepthPyramid(result, state.depthVisibility.GetStats().depthPyramid, context.frame);
                state.depthVisibilityValid = true;
                state.depthVisibilityViewProjValid = true;
                state.depthVisibilityViewProj = cameraCb.viewProj;
            } else {
                state.depthVisibilityValid = false;
                state.depthVisibilityViewProjValid = false;
            }
        } else if (!state.depthVisibilityBuildAllowedThisFrame) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
        }

        if (refreshBalancedSsao) {
            const bool ssaoOk =
                state.ssaoRenderer.RenderDepthOnly(*context.graphics, context.cmd, context.width, context.height,
                                                   context.sceneDepth.srv, cameraCb, environment.ambientOcclusion);
            state.ssaoValid = ssaoOk;
            if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
                result.ssaoRendered = true;
                result.ambientOcclusion = state.ssaoRenderer.GetAoView();
                GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
                RecordSsaoCompositeDebug(0.0f);
            }
        }
    }

    context.renderTargetAccess.EndDepthRead();
    return context.renderTargetAccess.Rebind();
}

bool ExecuteRayTracedShadowFromSceneDepth(ScreenSpaceRuntimeState& state,
                                          const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                          const RayTracedShadowInputs& inputs, ScreenSpaceFrameResult& result) {
    result.rayTracedShadow = {};
    if (context.graphics == nullptr || context.cmd == nullptr || !context.sceneDepth.IsSampleable() ||
        !context.renderTargetAccess.BeginDepthRead()) {
        return false;
    }
    RayTracedShadowInputs resolved = inputs;
    resolved.sceneDepth = context.sceneDepth;
    const bool rendered = state.rayTracedShadowRenderer.Render(*context.graphics, resolved);
    context.renderTargetAccess.EndDepthRead();
    if (!context.renderTargetAccess.Rebind() || !rendered) {
        return false;
    }
    result.rayTracedShadow = state.rayTracedShadowRenderer.GetVisibilityView();
    return result.rayTracedShadow.IsSampleable();
}

bool BuildCurrentFrameDepthPyramid(ScreenSpaceRuntimeState& state,
                                   const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                   const MESHRENDERER::CameraCB& renderCameraCb, ScreenSpaceFrameResult& result) {
    if (context.graphics == nullptr || context.cmd == nullptr || !context.sceneDepth.IsSampleable() ||
        !context.renderTargetAccess.BeginDepthRead()) {
        return false;
    }
    result.hzbBuilt = state.depthVisibility.BuildDepthPyramidFromDepthSrv(
        *context.graphics, context.cmd, context.width, context.height, context.sceneDepth.srv, renderCameraCb.viewProj,
        context.jitteredViewProj);
    context.renderTargetAccess.EndDepthRead();
    if (!context.renderTargetAccess.Rebind() || !result.hzbBuilt) {
        return false;
    }
    ++gDepthVisibilityDebugState.sceneDepthPyramidBuildCount;
    state.depthVisibilityValid = true;
    PublishVisibilityDepthPyramid(result, state.depthVisibility.GetStats().depthPyramid,
                                  DEPTH::DepthPyramidViewKind::CurrentFrame, context.frame);
    RecordDepthPyramidSource(DepthVisibilitySource::CurrentSceneDepth);
    RecordDepthVisibilitySource(DepthVisibilitySource::CurrentSceneDepth);
    return true;
}

bool ExecuteBalancedSsaoFromSceneDepth(ScreenSpaceRuntimeState& state,
                                       const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
                                       const MESHRENDERER::CameraCB& renderCameraCb,
                                       const SceneEnvironment& environment, ScreenSpaceFrameResult& result) {
    state.balancedSsaoSameFrame = false;
    const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);
    if (ssaoMode != SsaoMode::Balanced || context.graphics == nullptr || context.cmd == nullptr ||
        !context.sceneDepth.IsSampleable()) {
        return false;
    }

    if (!context.renderTargetAccess.BeginDepthRead()) {
        return false;
    }

    GFX::PIX::ScopedGpuEvent pixSsao(context.cmd, GFX::PIX::kColorPost, "SSAO.BalancedFromCurrentDepth");
    const bool ssaoOk =
        state.ssaoRenderer.RenderDepthOnly(*context.graphics, context.cmd, context.width, context.height,
                                           context.sceneDepth.srv, renderCameraCb, environment.ambientOcclusion);
    context.renderTargetAccess.EndDepthRead();
    if (!context.renderTargetAccess.Rebind()) {
        return false;
    }

    state.ssaoValid = ssaoOk;
    if (!ssaoOk || state.ssaoRenderer.GetAoSrv().ptr == 0) {
        return false;
    }

    state.balancedSsaoSameFrame = true;
    result.ssaoRendered = true;
    result.ambientOcclusion = state.ssaoRenderer.GetAoView();
    return true;
}

} // namespace HIKARI::RENDER3D::SCREENSPACE
