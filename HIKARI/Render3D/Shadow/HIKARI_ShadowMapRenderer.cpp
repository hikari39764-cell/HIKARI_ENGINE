#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

#include <algorithm>

#include "HIKARI_Services.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

namespace HIKARI::SHADOW {

    using namespace INTERNAL;

    void Reset() {
        ClearShadowFrameSubmissions();
        InvalidateShadowCache();
        SetGpuDrivenSceneSource(nullptr);
    }

    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::ShadowPrepare);
        ClearShadowFrameSubmissions();
        gShadowRendererState.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        gShadowRendererState.debugStats.enabled = gShadowRendererState.frameEnabled;
        gShadowRendererState.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        gShadowRendererState.debugStats.worldTexelSize =
            std::max(1.0f, environment.directionalShadow.orthoSize) /
            static_cast<float>((std::max)(1u, gShadowRendererState.debugStats.resolution));
        gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
        gShadowRendererState.debugStats.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        gShadowRendererState.debugStats.pcfRadius = environment.directionalShadow.pcfRadius;
        gShadowRendererState.debugStats.orthoSize = environment.directionalShadow.orthoSize;
        gShadowRendererState.debugStats.nearPlane = environment.directionalShadow.nearPlane;
        gShadowRendererState.debugStats.farPlane =
            ResolveShadowDepthSpan(environment, std::max(1.0f, environment.directionalShadow.orthoSize));
        gShadowRendererState.debugStats.depthBias = environment.directionalShadow.depthBias;
        gShadowRendererState.debugStats.normalBias = environment.directionalShadow.normalBias;
        gShadowRendererState.debugStats.strength = environment.directionalShadow.strength;
        if (!gShadowRendererState.frameEnabled) {
            PublishShadowCacheStats();
            return;
        }
        gShadowRendererState.frameHasShadowWork = BuildShadowGpuDrivenSceneSources();
        if (!gShadowRendererState.frameHasShadowWork) {
            PublishShadowCacheStats();
            return;
        }
        if (!EnsureShadowRendererInitialized()) {
            gShadowRendererState.frameEnabled = false;
            gShadowRendererState.debugStats.enabled = false;
            gShadowRendererState.frameHasShadowWork = false;
            InvalidateShadowCache();
            return;
        }
        ActivateShadowFrameResources(SERVICES::gCtx.frameIndex);
        UploadShadowMeshShaderJointPalettes();

        const uint32_t resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (gShadowRendererState.shadowMap == nullptr || gShadowRendererState.resolution != resolution) {
            if (!CreateShadowMapResources(resolution)) {
                gShadowRendererState.frameEnabled = false;
                gShadowRendererState.debugStats.enabled = false;
                gShadowRendererState.frameHasShadowWork = false;
                InvalidateShadowCache();
                return;
            }
            gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
        }
        const ShadowLightFrame shadowFrame =
            BuildShadowLightFrame(environment, camera, resolution);
        gShadowRendererState.lightViewProj = shadowFrame.viewProj;
        gShadowRendererState.lightCullPosition = shadowFrame.lightPosition;
        gShadowRendererState.lightAnchor = shadowFrame.anchor;
        gShadowRendererState.lightAnchorGrid = shadowFrame.anchorGrid;
        UploadShadowCameraConstants(shadowFrame);
        if (CanReuseShadowCache(resolution)) {
            MarkShadowCacheHit();
            SubmitShadowDebugFrustum(environment, camera);
            return;
        }

        MarkShadowCacheMiss();
        SubmitShadowDebugFrustum(environment, camera);
    }

    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        gShadowRendererState.gpuDrivenSceneSource = source;
        if (source == nullptr) {
            gShadowRendererState.shadowSceneSource.Reset();
            gShadowRendererState.staticShadowSceneSource.Reset();
            gShadowRendererState.dynamicShadowSceneSource.Reset();
            gShadowRendererState.activeShadowSceneSource.Reset();
            gShadowRendererState.staticShadowPrimaryInstances.clear();
            gShadowRendererState.staticShadowPrimaryMaterialSources.clear();
            gShadowRendererState.dynamicShadowPrimaryInstances.clear();
            gShadowRendererState.dynamicShadowPrimaryMaterialSources.clear();
            ClearShadowTraditionalIndirectStreams();
            InvalidateShadowSourceCache();
        }
    }

    void InvalidateSceneCache() {
        gShadowRendererState.shadowSceneSource.Reset();
        gShadowRendererState.staticShadowSceneSource.Reset();
        gShadowRendererState.dynamicShadowSceneSource.Reset();
        gShadowRendererState.activeShadowSceneSource.Reset();
        ClearShadowTraditionalIndirectStreams();
        // Keep the static depth cache as a candidate.  The next rebuilt static
        // source is checked against its content key before any reuse, allowing a
        // dynamic-only refresh to keep valid static shadows without accepting
        // stale static geometry.
        InvalidateShadowSourceCache();
    }

    void RenderDirectionalShadowMap() {
        if (!gShadowRendererState.frameEnabled || !gShadowRendererState.frameHasShadowWork || gShadowRendererState.shadowMap == nullptr) {
            return;
        }
        auto* cmd = SERVICES::gCtx.cmdList;
        if (cmd == nullptr) {
            return;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer gpuShadow(cmd, GFX::GPU_PROFILE::Pass::ShadowMap);

        bool finalHasDepth = false;
        bool staticRendered = false;
        bool dynamicRendered = false;
        bool unifiedRendered = false;
        bool fallbackRendered = false;

        if (gShadowRendererState.shadowCache.WasHitThisFrame()) {
            if (!CopyStaticShadowCacheToFinal(cmd)) {
                MarkShadowCacheMiss();
            } else {
                finalHasDepth = true;
            }
        }

        // The shadow GPU-driven context owns one transient upload/cull submission
        // per frame.  Preparing static and dynamic sources back-to-back would make
        // both queued GPU copies read the last CPU upload page, corrupting the
        // static cache exactly when a skinned caster enters the scene.  If there
        // is no reusable static depth, render the combined source once instead.
        const bool needsUnifiedDynamicRender =
            gShadowRendererState.frameHasDynamicShadowWork && !finalHasDepth;
        if (needsUnifiedDynamicRender) {
            PrepareFinalShadowMapForDepthWrite(cmd, true);
            if (PrepareShadowSourceForDraw(gShadowRendererState.shadowSceneSource)) {
                unifiedRendered = ExecuteShadowGpuDrivenPass(
                    GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                    GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
            }
            finalHasDepth = unifiedRendered;
            gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
        } else {
            if (!finalHasDepth && gShadowRendererState.frameHasStaticShadowWork) {
                PrepareFinalShadowMapForDepthWrite(cmd, true);
                if (PrepareShadowSourceForDraw(gShadowRendererState.staticShadowSceneSource)) {
                    staticRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic);
                }
                if (staticRendered) {
                    finalHasDepth = true;
                    (void)UpdateStaticShadowCacheFromFinal(cmd);
                    gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(true);
                } else {
                    InvalidateShadowCache();
                }
            }

            const bool staticSplitFailed =
                !gShadowRendererState.shadowCache.WasHitThisFrame() &&
                gShadowRendererState.frameHasStaticShadowWork &&
                !staticRendered;

            if (!staticSplitFailed && gShadowRendererState.frameHasDynamicShadowWork) {
                PrepareFinalShadowMapForDepthWrite(cmd, false);
                if (PrepareShadowSourceForDraw(gShadowRendererState.dynamicShadowSceneSource)) {
                    dynamicRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic);
                }
                if (dynamicRendered) {
                    gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
                } else {
                    // Keep the copied static depth this frame.  Invalidating the
                    // cache makes the next frame use the single-submit unified path.
                    InvalidateShadowCache();
                }
            }

            if (staticSplitFailed) {
                PrepareFinalShadowMapForDepthWrite(cmd, true);
                if (PrepareShadowSourceForDraw(gShadowRendererState.shadowSceneSource)) {
                    fallbackRendered = ExecuteShadowGpuDrivenPass(
                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
                }
                finalHasDepth = fallbackRendered;
                if (fallbackRendered) {
                    gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
                }
            }
        }

        if (!finalHasDepth) {
            PrepareFinalShadowMapForDepthWrite(cmd, true);
            finalHasDepth = true;
        }

        gShadowRendererState.debugStats.shadowStaticRendered = staticRendered;
        gShadowRendererState.debugStats.shadowDynamicRendered = dynamicRendered;
        gShadowRendererState.debugStats.shadowUnifiedRendered = unifiedRendered;
        gShadowRendererState.debugStats.shadowFallbackRendered = fallbackRendered;
        FinishFinalShadowMap(cmd);
    }

    bool IsDirectionalShadowEnabled() {
        return gShadowRendererState.frameEnabled &&
            gShadowRendererState.frameHasShadowWork &&
            gShadowRendererState.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(gShadowRendererState.shadowSrvResource);
    }

    const MATH::Mat4& GetDirectionalLightViewProj() {
        return gShadowRendererState.lightViewProj;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
        return RENDER3D::GetTextureResourceSrvGpuHandle(gShadowRendererState.shadowSrvResource);
    }

    uint32_t GetShadowResolution() {
        return gShadowRendererState.resolution;
    }

    float GetShadowStrength() {
        return 0.75f;
    }

    float GetDepthBias() {
        return 0.001f;
    }

    float GetNormalBias() {
        return 0.02f;
    }

    const ShadowMapDebugStats& GetDebugStats() {
        return gShadowRendererState.debugStats;
    }

} // namespace HIKARI::SHADOW
