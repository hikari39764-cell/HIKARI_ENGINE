#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

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
                    maxDelta = (std::max)(
                        maxDelta,
                        std::fabs(lhs.m[col][row] - rhs.m[col][row]));
                }
            }
            return maxDelta;
        }

        bool IsDepthPyramidStatsUsable(
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
            uint32_t width,
            uint32_t height) {

            const HIKARI::RENDER3D::DEPTH::DepthPyramidView& pyramid =
                stats.depthPyramid;
            return
                pyramid.valid &&
                pyramid.pyramidSrv.ptr != 0 &&
                pyramid.sourceWidth == width &&
                pyramid.sourceHeight == height &&
                pyramid.width != 0 &&
                pyramid.height != 0 &&
                pyramid.viewProjValid;
        }

        bool IsHistoryDepthPyramidUsableForCullingView(
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& stats,
            uint32_t width,
            uint32_t height,
            const MATH::Mat4& viewProj) {

            if (!IsDepthPyramidStatsUsable(stats, width, height)) {
                return false;
            }

            constexpr float kHistoryViewProjMaxDelta = 0.0125f;
            return MaxAbsDeltaMat4(stats.depthPyramid.viewProj, viewProj) <=
                kHistoryViewProjMaxDelta;
        }

        void ClearFrozenCullingDepthStats(ScreenSpaceRuntimeState& state) {
            state.frozenCullingDepthStats = {};
            state.frozenCullingDepthStatsValid = false;
        }

        void PublishFrameDepthPyramid(
            ScreenSpaceFrameResult& result,
            DEPTH::DepthPyramidView view,
            DEPTH::DepthPyramidViewKind viewKind) {

            if (!view.valid ||
                view.pyramidSrv.ptr == 0 ||
                view.width == 0 ||
                view.height == 0) {
                return;
            }

            view.viewKind = viewKind;
            result.hzbBuilt = true;
            result.depthPyramidBuilt = true;
            result.depthPyramid = view;
            DEPTH::PublishFrameDepthPyramid(view);
        }

        void BeginDepthVisibilityDebugFrame(
            uint32_t width,
            uint32_t height,
            bool historyReady,
            bool historyMatched) {

            gDepthVisibilityDebugState = {};
            gDepthVisibilityDebugState.width = width;
            gDepthVisibilityDebugState.height = height;
            gDepthVisibilityDebugState.historyReady = historyReady;
            gDepthVisibilityDebugState.historyMatched = historyMatched;
        }

        void RecordDepthVisibilitySource(DepthVisibilitySource source) {
            gDepthVisibilityDebugState.visibilitySource = source;
            gDepthVisibilityDebugState.visibilityUsedHzb =
                source != DepthVisibilitySource::None &&
                source != DepthVisibilitySource::NoHzb;
            gDepthVisibilityDebugState.visibilityWithoutHzb =
                source == DepthVisibilitySource::NoHzb;
            if (gDepthVisibilityDebugState.visibilityUsedHzb) {
                gDepthVisibilityDebugState.depthPyramidBuilt = true;
                gDepthVisibilityDebugState.latestPyramidSource = source;
            }
        }

        void RecordDepthPyramidSource(DepthVisibilitySource source) {
            if (source == DepthVisibilitySource::None ||
                source == DepthVisibilitySource::NoHzb) {
                return;
            }
            gDepthVisibilityDebugState.depthPyramidBuilt = true;
            gDepthVisibilityDebugState.latestPyramidSource = source;
        }

        void RecordDepthVisibilityWithoutHzb() {
            RecordDepthVisibilitySource(DepthVisibilitySource::NoHzb);
        }

        // History HZB が使えないフレームでも遮蔽剔除を丸ごと失わないよう、
        // occluder のみの depth prepass から当該フレームの depth pyramid を構築し、
        // GPU-driven visibility を確定する。pyramid が成立したら true を返す。
        bool BuildDepthVisibilityFromOccluderPrepass(
            ScreenSpaceRuntimeState& state,
            const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
            const MESHRENDERER::CameraCB& cullingCameraCb,
            ScreenSpaceFrameResult& result) {

            bool depthWritten = false;
            if (MESHRENDERER::HasDepthPrepassWork()) {
                const D3D12_CPU_DESCRIPTOR_HANDLE visibilityDsv =
                    state.depthVisibility.BeginDepthPrepass(
                        context.cmd,
                        context.width,
                        context.height);
                depthWritten =
                    visibilityDsv.ptr != 0 &&
                    MESHRENDERER::RenderDepthPrepass(visibilityDsv);
            }
            result.depthPrepassWritten = depthWritten;
            state.depthVisibility.RecordDepthPrepass(depthWritten);
            gDepthVisibilityDebugState.depthPrepassWritten = depthWritten;
            if (depthWritten) {
                result.hzbBuilt =
                    state.depthVisibility.BuildDepthPyramidFromVisibilityPrepass(
                        context.cmd,
                        context.width,
                        context.height,
                        cullingCameraCb.viewProj);
            }

            context.renderTargetAccess.Rebind();
            const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& currentDepthStats =
                state.depthVisibility.GetStats();
            state.depthVisibilityValid =
                IsDepthPyramidStatsUsable(
                    currentDepthStats,
                    context.width,
                    context.height);
            state.depthVisibilityViewProjValid = state.depthVisibilityValid;
            state.depthVisibilityViewProj = cullingCameraCb.viewProj;
            if (!state.depthVisibilityValid) {
                RecordDepthVisibilityWithoutHzb();
                (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();
                return false;
            }

            RecordDepthPyramidSource(DepthVisibilitySource::CurrentPrepass);
            PublishFrameDepthPyramid(
                result,
                currentDepthStats.depthPyramid,
                DEPTH::DepthPyramidViewKind::CurrentFrame);
            RecordDepthVisibilitySource(DepthVisibilitySource::CurrentPrepass);
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                currentDepthStats);
            return true;
        }
    }

    ScreenSpaceRuntimeState& GetScreenSpaceRuntimeState() {
        return gScreenSpaceState;
    }

    const DepthVisibilityDebugState& GetDepthVisibilityDebugState() {
        return gDepthVisibilityDebugState;
    }

    const char* ToString(DepthVisibilitySource source) {
        switch (source) {
        case DepthVisibilitySource::History:
            return "History";
        case DepthVisibilitySource::Frozen:
            return "Frozen";
        case DepthVisibilitySource::CurrentPrepass:
            return "CurrentPrepass";
        case DepthVisibilitySource::CurrentSceneDepth:
            return "CurrentSceneDepth";
        case DepthVisibilitySource::NoHzb:
            return "NoHzb";
        case DepthVisibilitySource::None:
        default:
            return "None";
        }
    }

    void ReleaseScreenSpaceRuntimeState() {
        gScreenSpaceState.geometryAux.Release();
        gScreenSpaceState.ssaoRenderer.Release();
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
            state.fallbackAoTextureHandle =
                RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
            return state.fallbackAoTextureHandle >= 0;
        }

        state.fallbackAoTextureHandle = -1;
        state.fallbackAoTextureResource = RENDER3D::CreateSolidColorTextureResource(
            "screen_space/fallback_ao",
            0xffffffffu,
            RENDER3D::TextureResourceColorSpace::Linear);
        state.fallbackAoTextureHandle =
            RENDER3D::GetTextureResourceBackendHandle(state.fallbackAoTextureResource);
        return RENDER3D::IsTextureResourceValid(state.fallbackAoTextureResource) &&
            state.fallbackAoTextureHandle >= 0;
    }

    ScreenSpaceFrameResult ExecuteScreenSpacePreLightingPasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& renderCameraCb,
        const MESHRENDERER::CameraCB& cullingCameraCb,
        const SceneEnvironment& environment) {

        ScreenSpaceFrameResult result{};
        DEPTH::BeginDepthPyramidFrame(SERVICES::gCtx.frameIndex);
        const bool fallbackReady = EnsureScreenSpaceFallbacks(state);
        if (fallbackReady) {
            result.fallbackAoTextureResource = state.fallbackAoTextureResource;
            result.aoSrv = RENDER3D::GetTextureResourceSrvGpuHandle(state.fallbackAoTextureResource);
        }
        result.fallbackAoTextureHandle = state.fallbackAoTextureHandle;
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats historyDepthStats =
            state.depthVisibility.GetStats();
        const bool historyDepthPyramidReady =
            IsDepthPyramidStatsUsable(historyDepthStats, context.width, context.height);
        const bool historyDepthPyramidMatchesCullingView =
            IsHistoryDepthPyramidUsableForCullingView(
                historyDepthStats,
                context.width,
                context.height,
                cullingCameraCb.viewProj);
        state.depthVisibility.ResetFrame();
        BeginDepthVisibilityDebugFrame(
            context.width,
            context.height,
            historyDepthPyramidReady,
            historyDepthPyramidMatchesCullingView);
        BeginSsaoDebugFrame(context.width, context.height, environment.ambientOcclusion);

        if (context.cmd == nullptr) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            state.ssaoValid = false;
            return result;
        }

        // Screen-space pass は forward lighting の前に必要な texture を生成する。
        GFX::PIX::ScopedGpuEvent pixScreenSpace(context.cmd, GFX::PIX::kColorPost, "ScreenSpace.PreLighting");

        const SsaoMode ssaoMode = ResolveEffectiveSsaoMode(environment.ambientOcclusion);

        const CpuClock::time_point depthVisibilityStart = CpuClock::now();
        const bool depthVisibilityAllowedThisFrame = true;
        state.depthVisibilityBuildAllowedThisFrame =
            depthVisibilityAllowedThisFrame;
        const bool cullingCameraFrozen =
            MESHRENDERER::IsGpuDrivenCullingCameraFrozen();
        if (!cullingCameraFrozen) {
            ClearFrozenCullingDepthStats(state);
        }

        if (cullingCameraFrozen) {
            if (!state.frozenCullingDepthStatsValid &&
                historyDepthPyramidMatchesCullingView) {
                state.frozenCullingDepthStats = historyDepthStats;
                state.frozenCullingDepthStatsValid = true;
            }

            const bool frozenDepthReady =
                state.frozenCullingDepthStatsValid &&
                IsHistoryDepthPyramidUsableForCullingView(
                    state.frozenCullingDepthStats,
                    context.width,
                    context.height,
                    cullingCameraCb.viewProj);
            if (frozenDepthReady) {
                GFX::PIX::ScopedGpuEvent pixFrozen(
                    context.cmd,
                    GFX::PIX::kColorUpload,
                    "GpuDepthVisibility.UseFrozenDepthPyramid");
                state.depthVisibilityValid = true;
                state.depthVisibilityViewProjValid = true;
                state.depthVisibilityViewProj =
                    state.frozenCullingDepthStats.depthPyramid.viewProj;
                PublishFrameDepthPyramid(
                    result,
                    state.frozenCullingDepthStats.depthPyramid,
                    DEPTH::DepthPyramidViewKind::History);
                RecordDepthVisibilitySource(DepthVisibilitySource::Frozen);
                (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                    state.frozenCullingDepthStats);
            } else if (BuildDepthVisibilityFromOccluderPrepass(
                    state,
                    context,
                    cullingCameraCb,
                    result)) {
                state.frozenCullingDepthStats = state.depthVisibility.GetStats();
                state.frozenCullingDepthStatsValid = true;
                RecordDepthVisibilitySource(DepthVisibilitySource::Frozen);
            } else {
                ClearFrozenCullingDepthStats(state);
            }
        } else if (depthVisibilityAllowedThisFrame &&
            historyDepthPyramidReady &&
            historyDepthPyramidMatchesCullingView) {
            GFX::PIX::ScopedGpuEvent pixHistory(
                context.cmd,
                GFX::PIX::kColorUpload,
                "GpuDepthVisibility.UseHistoryDepthPyramid");
            state.depthVisibilityValid = true;
            state.depthVisibilityViewProjValid = true;
            state.depthVisibilityViewProj = historyDepthStats.depthPyramid.viewProj;
            PublishFrameDepthPyramid(
                result,
                historyDepthStats.depthPyramid,
                DEPTH::DepthPyramidViewKind::History);
            RecordDepthVisibilitySource(DepthVisibilitySource::History);
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityFromDepth(
                historyDepthStats);
        } else if (historyDepthPyramidReady) {
            // History pyramid は健在だが視点が動いて許容差を超えたフレーム。
            // カメラ移動中は毎フレームここに来るため、occluder prepass の再構築
            // (full depth 描画 + pyramid build) は高くつく。frustum のみに落とし、
            // ピクセル過描画は scene depth prepass に任せる。
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            RecordDepthVisibilityWithoutHzb();
            (void)MESHRENDERER::FinalizeGpuDrivenVisibilityWithoutDepth();
        } else {
            // Pyramid が構造的に無い (初回 / resize 直後)。occluder prepass から
            // 当該フレームの pyramid を作って遮蔽剔除を確保する。
            (void)BuildDepthVisibilityFromOccluderPrepass(
                state,
                context,
                cullingCameraCb,
                result);
        }

        const float depthVisibilityMs =
            ElapsedMs(depthVisibilityStart, CpuClock::now());

        // Off 時は GeometryAux も作らない。
        if (ssaoMode == SsaoMode::Off ||
            environment.ambientOcclusion.editorViewportSuppressed) {
            state.ssaoRenderer.RecordSkipped(
                context.width,
                context.height,
                environment.ambientOcclusion);
            state.geometryValid = false;
            state.ssaoValid = false;
            return result;
        }

        if (ssaoMode == SsaoMode::Balanced) {
            RecordSsaoGeometryAuxDebug(
                false,
                depthVisibilityMs,
                DXGI_FORMAT_UNKNOWN);
            state.geometryValid = false;

            if (state.ssaoValid &&
                state.ssaoRenderer.IsValid() &&
                state.ssaoRenderer.GetAoSrv().ptr != 0) {
                result.ssaoRendered = true;
                result.aoSrv = state.ssaoRenderer.GetAoSrv();
                GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
                RecordSsaoCompositeDebug(0.0f);
            } else {
                state.ssaoValid = false;
                result.ssaoRendered = false;
            }
            return result;
        }

        GFX::PIX::ScopedGpuEvent pixGeometry(context.cmd, GFX::PIX::kColorRender, "GeometryAux");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuGeometry(
            context.cmd,
            GFX::GPU_PROFILE::Pass::GeometryAux);
        const CpuClock::time_point geometryStart = CpuClock::now();
        result.geometryAuxWritten = MESHRENDERER::RenderGeometryAuxPass(
            state.geometryAux,
            context.sceneDsv);
        context.renderTargetAccess.Rebind();
        RecordSsaoGeometryAuxDebug(
            result.geometryAuxWritten,
            ElapsedMs(geometryStart, CpuClock::now()),
            state.geometryAux.GetFormat());

        state.geometryValid = result.geometryAuxWritten && state.geometryAux.IsValid();
        if (!state.geometryValid ||
            !context.depthReadable ||
            context.sceneDepthSrv.ptr == 0) {
            state.ssaoValid = false;
            return result;
        }

        bool ssaoOk = false;
        if (context.renderTargetAccess.BeginDepthRead()) {
            ssaoOk = state.ssaoRenderer.Render(
                context.cmd,
                state.geometryAux,
                context.sceneDepthSrv,
                renderCameraCb,
                environment.ambientOcclusion);
            context.renderTargetAccess.EndDepthRead();
            // SSAO は内部 AO RT を複数回 bind するので、lighting pass の前に scene RT へ戻す。
            context.renderTargetAccess.Rebind();
        }

        state.ssaoValid = ssaoOk;
        result.ssaoRendered = ssaoOk;
        if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
            result.aoSrv = state.ssaoRenderer.GetAoSrv();
            GFX::PIX::SetGpuMarker(context.cmd, GFX::PIX::kColorPost, "SSAO.Composite");
            RecordSsaoCompositeDebug(0.0f);
        }
        return result;
    }

    bool ExecuteScreenSpacePostOpaquePasses(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& cameraCb,
        const SceneEnvironment& environment,
        ScreenSpaceFrameResult& result) {

        if (context.cmd == nullptr) {
            return false;
        }

        const bool depthAvailable =
            context.depthReadable &&
            context.sceneDepthSrv.ptr != 0;
        if (!depthAvailable) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            return context.renderTargetAccess.Rebind();
        }

        const SsaoMode ssaoMode =
            ResolveEffectiveSsaoMode(environment.ambientOcclusion);
        // 同フレーム経路 (ExecuteBalancedSsaoFromSceneDepth) が成功した場合は
        // temporal 更新を省く。フラグはここで毎フレーム消費する。
        const bool refreshBalancedSsao =
            ssaoMode == SsaoMode::Balanced &&
            !environment.ambientOcclusion.editorViewportSuppressed &&
            !state.balancedSsaoSameFrame;
        state.balancedSsaoSameFrame = false;

        if (!context.renderTargetAccess.BeginDepthRead()) {
            state.depthVisibilityValid = false;
            state.depthVisibilityViewProjValid = false;
            return false;
        }

        {
            GFX::PIX::ScopedGpuEvent pixPostOpaque(
                context.cmd,
                GFX::PIX::kColorPost,
                "ScreenSpace.PostOpaqueTemporal");

            if (state.depthVisibilityBuildAllowedThisFrame &&
                !MESHRENDERER::IsGpuDrivenCullingCameraFrozen()) {
                result.hzbBuilt = state.depthVisibility.BuildDepthPyramidFromDepthSrv(
                    context.cmd,
                    context.width,
                    context.height,
                    context.sceneDepthSrv,
                    cameraCb.viewProj);
                if (result.hzbBuilt) {
                    RecordDepthPyramidSource(DepthVisibilitySource::CurrentSceneDepth);
                    PublishFrameDepthPyramid(
                        result,
                        state.depthVisibility.GetStats().depthPyramid,
                        DEPTH::DepthPyramidViewKind::CurrentFrame);
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
                const bool ssaoOk = state.ssaoRenderer.RenderDepthOnly(
                    context.cmd,
                    context.width,
                    context.height,
                    context.sceneDepthSrv,
                    cameraCb,
                    environment.ambientOcclusion);
                state.ssaoValid = ssaoOk;
                if (ssaoOk && state.ssaoRenderer.GetAoSrv().ptr != 0) {
                    result.ssaoRendered = true;
                    result.aoSrv = state.ssaoRenderer.GetAoSrv();
                    GFX::PIX::SetGpuMarker(
                        context.cmd,
                        GFX::PIX::kColorPost,
                        "SSAO.Composite");
                    RecordSsaoCompositeDebug(0.0f);
                }
            }
        }

        context.renderTargetAccess.EndDepthRead();
        return context.renderTargetAccess.Rebind();
    }

    bool ExecuteBalancedSsaoFromSceneDepth(
        ScreenSpaceRuntimeState& state,
        const RENDER3D::PIPELINE::ScreenSpacePassContext& context,
        const MESHRENDERER::CameraCB& renderCameraCb,
        const SceneEnvironment& environment,
        ScreenSpaceFrameResult& result) {

        state.balancedSsaoSameFrame = false;
        const SsaoMode ssaoMode =
            ResolveEffectiveSsaoMode(environment.ambientOcclusion);
        if (ssaoMode != SsaoMode::Balanced ||
            environment.ambientOcclusion.editorViewportSuppressed ||
            context.cmd == nullptr ||
            context.sceneDepthSrv.ptr == 0) {
            return false;
        }

        if (!context.renderTargetAccess.BeginDepthRead()) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixSsao(
            context.cmd,
            GFX::PIX::kColorPost,
            "SSAO.BalancedFromScenePrepass");
        const bool ssaoOk = state.ssaoRenderer.RenderDepthOnly(
            context.cmd,
            context.width,
            context.height,
            context.sceneDepthSrv,
            renderCameraCb,
            environment.ambientOcclusion);
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
        result.aoSrv = state.ssaoRenderer.GetAoSrv();
        return true;
    }

} // namespace HIKARI::RENDER3D::SCREENSPACE
