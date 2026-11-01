#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"

namespace HIKARI::SHADOW {

using namespace INTERNAL;

void Reset() {
    ResetLocalLightShadows();
    ClearShadowFrameSubmissions(gShadowRendererState);
    gShadowRendererState.shadowCache = {};
    PublishShadowCacheStats(gShadowRendererState);
    SetGpuDrivenSceneSource(nullptr);
}

void BeginFrame(const GFX::Context& graphics, const SceneEnvironment& environment, const Camera3D& camera,
                RENDER3D::WORLD_SPRITES::WorldSpritePass* sprites) {
    CPU_PROFILE::ScopedCpuTimer cpuTimer(CPU_PROFILE::Pass::ShadowPrepare);
    ClearShadowFrameSubmissions(gShadowRendererState);
    gShadowRendererState.worldSprites = sprites;
    const RENDER3D::DirectionalShadowQualitySettings& quality = RENDER3D::GetRenderQualitySettings().directionalShadow;
    const EnvironmentDirectionalLight& primary = environment.frame.Primary();
    gShadowRendererState.frameEnabled =
        primary.illuminance * primary.horizonVisibility > 0.0f && environment.shadows.enabled;
    gShadowRendererState.debugStats.enabled = gShadowRendererState.frameEnabled;
    gShadowRendererState.debugStats.resolution = ResolveShadowResolution(quality.resolution);
    gShadowRendererState.debugStats.worldTexelSize = 0.0f;
    gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
    gShadowRendererState.debugStats.pcfEnabled = quality.pcfEnabled ? 1u : 0u;
    gShadowRendererState.debugStats.pcfRadius = quality.pcfRadius;
    gShadowRendererState.debugStats.orthoSize = 0.0f;
    gShadowRendererState.debugStats.nearPlane = quality.nearPlane;
    gShadowRendererState.debugStats.farPlane = quality.farPlane;
    gShadowRendererState.debugStats.depthBias = quality.depthBias;
    gShadowRendererState.debugStats.normalBias = quality.normalBias;
    gShadowRendererState.debugStats.strength = environment.shadows.strength;
    if (!gShadowRendererState.frameEnabled) {
        PublishShadowCacheStats(gShadowRendererState);
        return;
    }
    gShadowRendererState.frameHasShadowWork =
        BuildShadowGpuDrivenSceneSources(gShadowRendererState) || (sprites && sprites->HasShadowWork());
    if (!gShadowRendererState.frameHasShadowWork) {
        PublishShadowCacheStats(gShadowRendererState);
        return;
    }
    if (!EnsureShadowRendererInitialized(gShadowRendererState, graphics)) {
        gShadowRendererState.frameEnabled = false;
        gShadowRendererState.debugStats.enabled = false;
        gShadowRendererState.frameHasShadowWork = false;
        InvalidateShadowCache(gShadowRendererState);
        return;
    }
    ActivateShadowFrameResources(gShadowRendererState, graphics.frameIndex);
    const MATH::Mat4 materialViewProj = camera.GetViewProj();
    MESHRENDERER::FillCameraCB(materialViewProj, MATH::Inverse(materialViewProj), camera.GetPosition(),
                               static_cast<uint32_t>((std::max)(1, graphics.backBufferWidth)),
                               static_cast<uint32_t>((std::max)(1, graphics.backBufferHeight)),
                               gShadowRendererState.materialCamera);
    if (!UploadShadowMaterialCamera(gShadowRendererState, graphics)) {
        gShadowRendererState.frameEnabled = false;
        gShadowRendererState.debugStats.enabled = false;
        gShadowRendererState.frameHasShadowWork = false;
        InvalidateShadowCache(gShadowRendererState);
        return;
    }

    const uint32_t resolution = ResolveShadowResolution(quality.resolution);
    if (gShadowRendererState.shadowMap == nullptr || gShadowRendererState.resolution != resolution) {
        if (!CreateShadowMapResources(gShadowRendererState, graphics,
                                      {resolution, 2, true, "shadow/directional_map"})) {
            gShadowRendererState.frameEnabled = false;
            gShadowRendererState.debugStats.enabled = false;
            gShadowRendererState.frameHasShadowWork = false;
            InvalidateShadowCache(gShadowRendererState);
            return;
        }
        gShadowRendererState.debugStats.shadowMapRecreateCount = gShadowRendererState.shadowMapRecreateCount;
    }
    DirectionalShadowFrameSet shadowFrames = BuildDirectionalShadowFrameSet(environment, camera, resolution);
    std::array<float, kMaxDirectionalShadowCascades> minimumDepths{};
    for (uint32_t i = 0; i < shadowFrames.cascadeCount; ++i)
        minimumDepths[i] = shadowFrames.cascades[i].lightNear;
    const auto includeCasters = [&](const auto* instances) {
        if (instances == nullptr)
            return;
        for (const auto& instance : *instances) {
            if (!IsPrimaryShadowCaster(instance))
                continue;
            for (uint32_t i = 0; i < shadowFrames.cascadeCount; ++i)
                minimumDepths[i] =
                    (std::min)(minimumDepths[i],
                               GetShadowCasterMinimumDepth(shadowFrames.cascades[i], instance.boundsCenterRadius));
        }
    };
    const auto& casterPass =
        gShadowRendererState.shadowSceneSource.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    includeCasters(casterPass.instances);
    includeCasters(casterPass.traditionalIndirect.instances);
    if (sprites)
        for (const auto& bounds : sprites->GetShadowCasterBounds()) {
            for (uint32_t i = 0; i < shadowFrames.cascadeCount; ++i)
                minimumDepths[i] =
                    (std::min)(minimumDepths[i], GetShadowCasterMinimumDepth(shadowFrames.cascades[i], bounds));
        }
    for (uint32_t i = 0; i < shadowFrames.cascadeCount; ++i)
        ExtendShadowCasterDepth(shadowFrames.cascades[i], minimumDepths[i]);
    for (uint32_t i = shadowFrames.cascadeCount; i < kMaxDirectionalShadowCascades; ++i)
        shadowFrames.cascades[i] = shadowFrames.cascades[shadowFrames.cascadeCount - 1u];
    gShadowRendererState.cascadeCount = shadowFrames.cascadeCount;
    gShadowRendererState.cascadeSplitDistances = shadowFrames.splitDistances;
    gShadowRendererState.debugStats.cascadeCount = shadowFrames.cascadeCount;
    for (uint32_t cascadeIndex = 0; cascadeIndex < kMaxDirectionalShadowCascades; ++cascadeIndex) {
        gShadowRendererState.cascadeFrames[cascadeIndex] = shadowFrames.cascades[cascadeIndex];
        gShadowRendererState.cascadeLightViewProj[cascadeIndex] = shadowFrames.cascades[cascadeIndex].viewProj;
    }
    const ShadowLightFrame& primaryFrame = shadowFrames.cascades[0];
    gShadowRendererState.debugStats.orthoSize = primaryFrame.orthoSize;
    gShadowRendererState.debugStats.worldTexelSize =
        primaryFrame.orthoSize / static_cast<float>((std::max)(1u, resolution));
    gShadowRendererState.debugStats.nearPlane = primaryFrame.lightNear;
    gShadowRendererState.debugStats.farPlane = primaryFrame.lightFar;
    gShadowRendererState.activeCascadeIndex = 0u;
    gShadowRendererState.lightViewProj = primaryFrame.viewProj;
    gShadowRendererState.lightCullPosition = primaryFrame.lightPosition;
    gShadowRendererState.lightAnchor = primaryFrame.anchor;
    gShadowRendererState.lightAnchorGrid = primaryFrame.anchorGrid;
    if (CanReuseShadowCache(gShadowRendererState, resolution)) {
        MarkShadowCacheHit(gShadowRendererState);
        SubmitShadowDebugFrustum(gShadowRendererState, environment, camera);
        return;
    }

    MarkShadowCacheMiss(gShadowRendererState);
    SubmitShadowDebugFrustum(gShadowRendererState, environment, camera);
}

void SetGpuDrivenSceneSource(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {
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
        ClearShadowTraditionalIndirectStreams(gShadowRendererState);
        InvalidateShadowSourceCache(gShadowRendererState);
        gShadowRendererState.surfaceGpuSceneBuffer.InvalidateResidency();
        gShadowRendererState.gpuDrivenLayer.ResetFrame();
    }
}

void InvalidateSceneCache() {
    gShadowRendererState.shadowSceneSource.Reset();
    gShadowRendererState.staticShadowSceneSource.Reset();
    gShadowRendererState.dynamicShadowSceneSource.Reset();
    gShadowRendererState.activeShadowSceneSource.Reset();
    ClearShadowTraditionalIndirectStreams(gShadowRendererState);
    // Keep the static depth cache as a candidate.  The next rebuilt static
    // source is checked against its content key before any reuse, allowing a
    // dynamic-only refresh to keep valid static shadows without accepting
    // stale static geometry.
    InvalidateShadowSourceCache(gShadowRendererState);
}

void RenderDirectionalShadowMap(const GFX::Context& graphics) {
    if (!gShadowRendererState.frameEnabled || !gShadowRendererState.frameHasShadowWork ||
        gShadowRendererState.shadowMap == nullptr) {
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
    bool fallbackRendered = false;
    bool shadowDeformationPrepared = false;

    const auto renderCascades = [&](const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source, bool clearDepth,
                                    GFX::GPU_PROFILE::Pass traditionalPass, GFX::GPU_PROFILE::Pass meshletPass) {
        // Scene / Material は全 Cascade で不変。視点依存の剔除だけを各回記録する。
        if (!shadowDeformationPrepared) {
            if (!PrepareShadowDeformation(gShadowRendererState, graphics)) {
                return false;
            }
            shadowDeformationPrepared = true;
        }
        if (!PrepareShadowSourceForDraw(gShadowRendererState, graphics, source)) {
            return false;
        }
        bool renderedAll = true;
        for (uint32_t cascadeIndex = 0; cascadeIndex < gShadowRendererState.cascadeCount; ++cascadeIndex) {
            const ShadowLightFrame& cascade = gShadowRendererState.cascadeFrames[cascadeIndex];
            const LONG resolution = LONG(gShadowRendererState.resolution);
            const LONG left = LONG(cascadeIndex % 2u) * resolution;
            const LONG top = LONG(cascadeIndex / 2u) * resolution;
            if (!RecordShadowView(gShadowRendererState, graphics, cascade,
                                  {left, top, left + resolution, top + resolution}, clearDepth, traditionalPass,
                                  meshletPass)) {
                renderedAll = false;
            }
        }
        return renderedAll;
    };

    if (gShadowRendererState.shadowCache.WasHitThisFrame()) {
        if (!CopyStaticShadowCacheToFinal(gShadowRendererState, cmd)) {
            MarkShadowCacheMiss(gShadowRendererState);
        } else {
            finalHasDepth = true;
        }
    }

    // 静的深度を確立してから動的 caster を重ねる。各提出の upload は不変。
    if (!finalHasDepth && gShadowRendererState.frameHasStaticShadowWork) {
        staticRendered = renderCascades(gShadowRendererState.staticShadowSceneSource, true,
                                        GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic,
                                        GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic);
        if (staticRendered) {
            finalHasDepth = true;
            if (!UpdateStaticShadowCacheFromFinal(gShadowRendererState, cmd))
                InvalidateShadowCache(gShadowRendererState);
            gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(gShadowRendererState.shadowCache.IsValid());
        } else {
            InvalidateShadowCache(gShadowRendererState);
        }
    }

    const bool staticSplitFailed = !gShadowRendererState.shadowCache.WasHitThisFrame() &&
                                   gShadowRendererState.frameHasStaticShadowWork && !staticRendered;
    bool dynamicSplitFailed = false;
    if (!staticSplitFailed && gShadowRendererState.frameHasDynamicShadowWork) {
        dynamicRendered = renderCascades(gShadowRendererState.dynamicShadowSceneSource, !finalHasDepth,
                                         GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic,
                                         GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic);
        if (dynamicRendered) {
            finalHasDepth = true;
            gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
        } else {
            dynamicSplitFailed = true;
            InvalidateShadowCache(gShadowRendererState);
        }
    }

    if (staticSplitFailed || dynamicSplitFailed) {
        fallbackRendered = renderCascades(gShadowRendererState.shadowSceneSource, true,
                                          GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback,
                                          GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback);
        finalHasDepth = fallbackRendered;
        gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
    }

    if (!finalHasDepth) {
        for (uint32_t cascadeIndex = 0; cascadeIndex < gShadowRendererState.cascadeCount; ++cascadeIndex) {
            gShadowRendererState.activeCascadeIndex = cascadeIndex;
            PrepareFinalShadowMapForDepthWrite(gShadowRendererState, cmd, true);
        }
        finalHasDepth = true;
    }

    if (gShadowRendererState.worldSprites && gShadowRendererState.worldSprites->HasShadowWork()) {
        // 動くカードを静的キャッシュへ保存しない。次フレームは元の深度から合成する。
        gShadowRendererState.shadowCache.SetFinalMatchesStaticCache(false);
        for (uint32_t i = 0; i < gShadowRendererState.cascadeCount; ++i) {
            const LONG size = LONG(gShadowRendererState.resolution);
            const LONG x = LONG(i % 2u) * size, y = LONG(i / 2u) * size;
            if (!RecordWorldSpriteShadowView(gShadowRendererState, graphics, gShadowRendererState.cascadeFrames[i],
                                             {x, y, x + size, y + size}, false)) {
                gShadowRendererState.frameHasShadowWork = false;
                InvalidateShadowCache(gShadowRendererState);
                break;
            }
        }
    }
    gShadowRendererState.debugStats.shadowStaticRendered = staticRendered;
    gShadowRendererState.debugStats.shadowDynamicRendered = dynamicRendered;
    gShadowRendererState.debugStats.shadowFallbackRendered = fallbackRendered;
    gShadowRendererState.activeCascadeIndex = 0u;
    gShadowRendererState.lightViewProj = gShadowRendererState.cascadeLightViewProj[0];
    FinishFinalShadowMap(gShadowRendererState, graphics, cmd);
}

void SetDebugFrustumVisible(bool visible) noexcept { gShadowRendererState.debugFrustumVisible = visible; }

bool IsDirectionalShadowEnabled() {
    return gShadowRendererState.frameEnabled && gShadowRendererState.frameHasShadowWork &&
           gShadowRendererState.shadowMap != nullptr &&
           RENDER3D::IsTextureResourceValid(gShadowRendererState.shadowSrvResource);
}

const std::array<MATH::Mat4, kMaxDirectionalShadowCascades>& GetDirectionalCascadeLightViewProj() {
    return gShadowRendererState.cascadeLightViewProj;
}

const MATH::Vec4& GetDirectionalCascadeSplitDistances() { return gShadowRendererState.cascadeSplitDistances; }

uint32_t GetDirectionalCascadeCount() { return gShadowRendererState.cascadeCount; }

D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv() {
    return RENDER3D::GetTextureResourceSrvGpuHandle(gShadowRendererState.shadowSrvResource);
}

MATH::Vec4 GetDirectionalCascadeDepthBiasScales() {
    const auto& frames = gShadowRendererState.cascadeFrames;
    return {frames[0].depthBiasScale, frames[1].depthBiasScale, frames[2].depthBiasScale, frames[3].depthBiasScale};
}

float GetShadowAtlasTexelSize() { return 0.5f / static_cast<float>((std::max)(gShadowRendererState.resolution, 1u)); }

const ShadowMapDebugStats& GetDebugStats() { return gShadowRendererState.debugStats; }

} // namespace HIKARI::SHADOW
