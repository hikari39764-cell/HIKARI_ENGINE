#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

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

    void BeginFrame(
        const GFX::Context& graphics,
        const SceneEnvironment& environment,
        const Camera3D& camera) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::ShadowPrepare);
        ClearShadowFrameSubmissions();
        gShadowRendererState.frameEnabled = environment.directional.enabled && environment.directionalShadow.enabled;
        gShadowRendererState.debugStats.enabled = gShadowRendererState.frameEnabled;
        gShadowRendererState.debugStats.resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        gShadowRendererState.debugStats.worldTexelSize = 0.0f;
        gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
        gShadowRendererState.debugStats.pcfEnabled = environment.directionalShadow.pcfEnabled ? 1u : 0u;
        gShadowRendererState.debugStats.pcfRadius = environment.directionalShadow.pcfRadius;
        gShadowRendererState.debugStats.orthoSize = 0.0f;
        gShadowRendererState.debugStats.nearPlane = environment.directionalShadow.nearPlane;
        gShadowRendererState.debugStats.farPlane = environment.directionalShadow.farPlane;
        gShadowRendererState.debugStats.depthBias = environment.directionalShadow.depthBias;
        gShadowRendererState.debugStats.normalBias = environment.directionalShadow.normalBias;
        gShadowRendererState.debugStats.strength = environment.directionalShadow.strength;
        if (!gShadowRendererState.frameEnabled) {
            PublishShadowCacheStats();
            return;
        }
        gShadowRendererState.frameHasShadowWork =
            BuildShadowGpuDrivenSceneSources(graphics);
        if (!gShadowRendererState.frameHasShadowWork) {
            PublishShadowCacheStats();
            return;
        }
        if (!EnsureShadowRendererInitialized(graphics)) {
            gShadowRendererState.frameEnabled = false;
            gShadowRendererState.debugStats.enabled = false;
            gShadowRendererState.frameHasShadowWork = false;
            InvalidateShadowCache();
            return;
        }
        ActivateShadowFrameResources(graphics.frameIndex);
        UploadShadowMeshShaderJointPalettes();

        const uint32_t resolution = ResolveShadowResolution(environment.directionalShadow.resolution);
        if (gShadowRendererState.shadowMap == nullptr || gShadowRendererState.resolution != resolution) {
            if (!CreateShadowMapResources(graphics, resolution)) {
                gShadowRendererState.frameEnabled = false;
                gShadowRendererState.debugStats.enabled = false;
                gShadowRendererState.frameHasShadowWork = false;
                InvalidateShadowCache();
                return;
            }
            gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
        }
        const DirectionalShadowFrameSet shadowFrames =
            BuildDirectionalShadowFrameSet(environment, camera, resolution);
        gShadowRendererState.cascadeCount = shadowFrames.cascadeCount;
        gShadowRendererState.cascadeSplitDistances =
            shadowFrames.splitDistances;
        gShadowRendererState.debugStats.cascadeCount =
            shadowFrames.cascadeCount;
        for (uint32_t cascadeIndex = 0;
             cascadeIndex < kMaxDirectionalShadowCascades;
             ++cascadeIndex) {
            gShadowRendererState.cascadeFrames[cascadeIndex] =
                shadowFrames.cascades[cascadeIndex];
            gShadowRendererState.cascadeLightViewProj[cascadeIndex] =
                shadowFrames.cascades[cascadeIndex].viewProj;
            UploadShadowCameraConstants(
                shadowFrames.cascades[cascadeIndex],
                cascadeIndex);
        }
        const ShadowLightFrame& primaryFrame = shadowFrames.cascades[0];
        gShadowRendererState.debugStats.orthoSize = primaryFrame.orthoSize;
        gShadowRendererState.debugStats.worldTexelSize =
            primaryFrame.orthoSize /
            static_cast<float>((std::max)(1u, resolution));
        gShadowRendererState.debugStats.nearPlane = primaryFrame.lightNear;
        gShadowRendererState.debugStats.farPlane = primaryFrame.lightFar;
        gShadowRendererState.activeCascadeIndex = 0u;
        gShadowRendererState.lightViewProj = primaryFrame.viewProj;
        gShadowRendererState.lightCullPosition = primaryFrame.lightPosition;
        gShadowRendererState.lightAnchor = primaryFrame.anchor;
        gShadowRendererState.lightAnchorGrid = primaryFrame.anchorGrid;
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
            gShadowRendererState.surfaceGpuSceneBuffer.InvalidateResidency();
            gShadowRendererState.gpuDrivenLayer.ResetFrame();
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

    void RenderDirectionalShadowMap(const GFX::Context& graphics) {
        if (!gShadowRendererState.frameEnabled || !gShadowRendererState.frameHasShadowWork || gShadowRendererState.shadowMap == nullptr) {
            return;
        }
        auto* cmd = graphics.cmdList;
        if (cmd == nullptr) {
            return;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer gpuShadow(cmd, GFX::GPU_PROFILE::Pass::ShadowMap);

        bool finalHasDepth = false;
        bool staticRendered = false;
        bool dynamicRendered = false;
        bool unifiedRendered = false;
        bool fallbackRendered = false;

        const auto renderCascades = [&](
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source,
            bool clearDepth,
            GFX::GPU_PROFILE::Pass traditionalPass,
            GFX::GPU_PROFILE::Pass meshletPass) {
            // Scene / Material は全 Cascade で不変。視点依存の剔除だけを各回記録する。
            if (!PrepareShadowSourceForDraw(graphics, source)) {
                return false;
            }
            bool renderedAll = true;
            for (uint32_t cascadeIndex = 0;
                 cascadeIndex < gShadowRendererState.cascadeCount;
                 ++cascadeIndex) {
                const ShadowLightFrame& cascade =
                    gShadowRendererState.cascadeFrames[cascadeIndex];
                gShadowRendererState.activeCascadeIndex = cascadeIndex;
                gShadowRendererState.lightViewProj = cascade.viewProj;
                gShadowRendererState.lightCullPosition = cascade.lightPosition;
                gShadowRendererState.lightAnchor = cascade.anchor;
                gShadowRendererState.lightAnchorGrid = cascade.anchorGrid;
                PrepareFinalShadowMapForDepthWrite(cmd, clearDepth);
                BuildShadowGpuDrivenWorkFrame(graphics);
                if (!ExecuteShadowGpuDrivenPass(
                        graphics,
                        traditionalPass,
                        meshletPass)) {
                    renderedAll = false;
                }
            }
            return renderedAll;
        };

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
            unifiedRendered = renderCascades(
                gShadowRendererState.shadowSceneSource,
                true,
                GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
            finalHasDepth = unifiedRendered;
            gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
        } else {
            if (!finalHasDepth && gShadowRendererState.frameHasStaticShadowWork) {
                staticRendered = renderCascades(
                    gShadowRendererState.staticShadowSceneSource,
                    true,
                    GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic,
                    GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic);
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
                dynamicRendered = renderCascades(
                    gShadowRendererState.dynamicShadowSceneSource,
                    false,
                    GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic,
                    GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic);
                if (dynamicRendered) {
                    gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
                } else {
                    // Keep the copied static depth this frame.  Invalidating the
                    // cache makes the next frame use the single-submit unified path.
                    InvalidateShadowCache();
                }
            }

            if (staticSplitFailed) {
                fallbackRendered = renderCascades(
                    gShadowRendererState.shadowSceneSource,
                    true,
                    GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                    GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
                finalHasDepth = fallbackRendered;
                if (fallbackRendered) {
                    gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
                }
            }
        }

        if (!finalHasDepth) {
            for (uint32_t cascadeIndex = 0;
                 cascadeIndex < gShadowRendererState.cascadeCount;
                 ++cascadeIndex) {
                gShadowRendererState.activeCascadeIndex = cascadeIndex;
                PrepareFinalShadowMapForDepthWrite(cmd, true);
            }
            finalHasDepth = true;
        }

        gShadowRendererState.debugStats.shadowStaticRendered = staticRendered;
        gShadowRendererState.debugStats.shadowDynamicRendered = dynamicRendered;
        gShadowRendererState.debugStats.shadowUnifiedRendered = unifiedRendered;
        gShadowRendererState.debugStats.shadowFallbackRendered = fallbackRendered;
        gShadowRendererState.activeCascadeIndex = 0u;
        gShadowRendererState.lightViewProj =
            gShadowRendererState.cascadeLightViewProj[0];
        FinishFinalShadowMap(graphics, cmd);
    }

    bool IsDirectionalShadowEnabled() {
        return gShadowRendererState.frameEnabled &&
            gShadowRendererState.frameHasShadowWork &&
            gShadowRendererState.shadowMap != nullptr &&
            RENDER3D::IsTextureResourceValid(gShadowRendererState.shadowSrvResource);
    }

    const MATH::Mat4& GetDirectionalLightViewProj() {
        return gShadowRendererState.cascadeLightViewProj[0];
    }

    const std::array<MATH::Mat4, kMaxDirectionalShadowCascades>&
        GetDirectionalCascadeLightViewProj() {
        return gShadowRendererState.cascadeLightViewProj;
    }

    const MATH::Vec4& GetDirectionalCascadeSplitDistances() {
        return gShadowRendererState.cascadeSplitDistances;
    }

    uint32_t GetDirectionalCascadeCount() {
        return gShadowRendererState.cascadeCount;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
        return RENDER3D::GetTextureResourceSrvGpuHandle(gShadowRendererState.shadowSrvResource);
    }

    uint32_t GetShadowResolution() {
        return gShadowRendererState.resolution;
    }

    const ShadowMapDebugStats& GetDebugStats() {
        return gShadowRendererState.debugStats;
    }

} // namespace HIKARI::SHADOW
