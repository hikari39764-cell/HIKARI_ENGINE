#include "Render3D/Render/HIKARI_ModelRenderer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::MODELRENDERER {

    namespace {
        ModelRendererDebugStats gDebugStats{};
        RENDER3D::RUNTIME::RenderModelCache gRenderModelCache{};
        constexpr DXGI_FORMAT kSecondaryCaptureFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;

        struct SecondaryCaptureTargetState {
            std::array<
                std::unique_ptr<RenderTarget2D>,
                GFX::kFrameResourceCount> targets{};
            std::array<
                RENDER3D::TextureResourceHandle,
                GFX::kFrameResourceCount> textures{};
            uint64_t deviceGeneration = 0u;
            uint64_t worldIdentity = 0u;
            uint64_t outputRevision = 0u;
            uint32_t width = 0u;
            uint32_t height = 0u;
            uint32_t writeIndex = 0u;

            void Reset() noexcept {
                for (RENDER3D::TextureResourceHandle& texture : textures) {
                    if (texture) {
                        (void)RENDER3D::ReleaseTextureResource(texture);
                        texture = {};
                    }
                }
                targets = {};
                deviceGeneration = 0u;
                worldIdentity = 0u;
                outputRevision = 0u;
                width = 0u;
                height = 0u;
                writeIndex = 0u;
            }

            bool Ensure(
                const GFX::Context& graphics,
                uint64_t requestedWorldIdentity,
                uint32_t captureSlot,
                uint32_t requestedWidth,
                uint32_t requestedHeight) {
                if (graphics.device == nullptr ||
                    graphics.cmdList == nullptr ||
                    requestedWorldIdentity == 0u) {
                    return false;
                }
                const bool recreate =
                    deviceGeneration != graphics.deviceGeneration ||
                    worldIdentity != requestedWorldIdentity ||
                    width != requestedWidth ||
                    height != requestedHeight ||
                    std::any_of(
                        targets.begin(),
                        targets.end(),
                        [](const auto& target) {
                            return target == nullptr;
                        });
                if (recreate) {
                    Reset();
                    deviceGeneration = graphics.deviceGeneration;
                    worldIdentity = requestedWorldIdentity;
                    width = requestedWidth;
                    height = requestedHeight;
                    for (size_t index = 0u; index < targets.size(); ++index) {
                        targets[index] = std::make_unique<RenderTarget2D>();
                        targets[index]->UpdateContext(graphics);
                        targets[index]->SetDebugName(
                            "Runtime.SceneCapture." +
                            std::to_string(captureSlot) + "." +
                            std::to_string(index));
                        if (!targets[index]->Init(
                                static_cast<int>(width),
                                static_cast<int>(height),
                                kSecondaryCaptureFormat,
                                true,
                                { 0.015f, 0.02f, 0.035f, 1.0f },
                                false)) {
                            Reset();
                            return false;
                        }
                        RENDER3D::RenderResourceDesc resource{};
                        resource.kind = RENDER3D::RenderResourceKind::Texture;
                        resource.usage =
                            RENDER3D::RenderResourceUsageFlags::ShaderResource |
                            RENDER3D::RenderResourceUsageFlags::RenderTarget;
                        resource.lifetime =
                            RENDER3D::RenderResourceLifetime::External;
                        resource.debugName =
                            "Runtime.SceneCapture.Texture." +
                            std::to_string(captureSlot) + "." +
                            std::to_string(index);
                        resource.sourceKey =
                            "runtime-scene-capture:" +
                            std::to_string(requestedWorldIdentity) + ":" +
                            std::to_string(captureSlot) + ":" +
                            std::to_string(index);
                        resource.width = width;
                        resource.height = height;
                        resource.depthOrArraySize = 1u;
                        resource.mipLevels = 1u;
                        resource.format = kSecondaryCaptureFormat;
                        textures[index] =
                            RENDER3D::RegisterTextureResourceFromNative(
                                targets[index]->GetResource(),
                                kSecondaryCaptureFormat,
                                std::move(resource));
                        if (!textures[index]) {
                            Reset();
                            return false;
                        }
                    }
                }
                for (const auto& target : targets) {
                    target->UpdateContext(graphics);
                }
                return true;
            }
        };

        std::array<
            SecondaryCaptureTargetState,
            MESHRENDERER::kSecondarySceneCaptureMaximumCount>&
            SecondaryCaptureTargets() {
            static std::array<
                SecondaryCaptureTargetState,
                MESHRENDERER::kSecondarySceneCaptureMaximumCount> states{};
            return states;
        }

        void SyncRenderModelCacheStats() {
            const RENDER3D::RUNTIME::RenderModelCache::Stats& cacheStats =
                gRenderModelCache.GetStats();
            gDebugStats.cache.renderModelCacheRequestCount = cacheStats.requestCount;
            gDebugStats.cache.renderModelCacheHitCount = cacheStats.hitCount;
            gDebugStats.cache.renderModelCacheMissCount = cacheStats.missCount;
            gDebugStats.cache.renderModelCacheInvalidCount = cacheStats.invalidModelCount;
            gDebugStats.cache.renderModelCachedModelCount = cacheStats.cachedModelCount;
            gDebugStats.cache.renderModelCachedSurfaceCount = cacheStats.cachedSurfaceCount;
        }
    }

    const char* ToString(ModelRendererFrameKind kind) {
        switch (kind) {
        case ModelRendererFrameKind::MainView:
            return "MainView";
        case ModelRendererFrameKind::ReflectionProbeCapture:
            return "ReflectionProbeCapture";
        case ModelRendererFrameKind::LightProbeCapture:
            return "LightProbeCapture";
        case ModelRendererFrameKind::SecondarySceneCapture:
            return "SecondarySceneCapture";
        case ModelRendererFrameKind::None:
        default:
            return "None";
        }
    }

    void ResetModelRendererFrameStats() {
        gDebugStats.frame = {};
    }

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
        for (SecondaryCaptureTargetState& state :
             SecondaryCaptureTargets()) {
            state.Reset();
        }
        MESHRENDERER::Reset();
        SHADOW::Reset();
    }

    MESHRENDERER::MeshRendererFrameResult RenderAll(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        RenderDebugView debugView) {

        if (view.cameraFrame == nullptr || !view.cameraFrame->valid) {
            return {};
        }

        const Camera3D& camera = view.cameraFrame->camera;
        BeginModelRendererFrame(ModelRendererFrameKind::MainView);
        SHADOW::BeginFrame(graphics, environment, camera);
        SHADOW::RenderDirectionalShadowMap(graphics);
        return MESHRENDERER::RenderAll(
            graphics,
            view,
            environment,
            lighting,
            debugView);
    }

    void RenderOpaqueForReflectionProbeCapture(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t width,
        uint32_t height,
        ModelRendererFrameKind kind) {

        BeginModelRendererFrame(kind);
        (void)RENDER3D::PIPELINE::RenderMeshCaptureOpaqueFrame(
            graphics,
            camera,
            environment,
            lighting,
            width,
            height);
    }

    bool RenderSecondarySceneCapture(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t captureSlot,
        uint32_t width,
        uint32_t height,
        bool renderTransparent) {

        BeginModelRendererFrame(
            ModelRendererFrameKind::SecondarySceneCapture);
        return MESHRENDERER::RenderSecondarySceneCapture(
            graphics,
            view,
            environment,
            lighting,
            captureSlot,
            width,
            height,
            renderTransparent);
    }

    bool RenderSecondarySceneCaptureTarget(
        const GFX::Context& graphics,
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        ModelManager& modelManager,
        SkyManager& skyManager,
        uint64_t worldIdentity,
        uint32_t captureSlot,
        uint32_t width,
        uint32_t height,
        bool renderSky,
        bool renderTransparent,
        SecondarySceneCaptureOutput& outOutput) {

        outOutput = {};
        if (captureSlot >=
                MESHRENDERER::kSecondarySceneCaptureMaximumCount) {
            return false;
        }
        SecondaryCaptureTargetState& state =
            SecondaryCaptureTargets()[captureSlot];
        if (view.cameraFrame == nullptr ||
            !view.cameraFrame->valid ||
            !state.Ensure(
                graphics,
                worldIdentity,
                captureSlot,
                width,
                height)) {
            return false;
        }

        RenderTarget2D& target = *state.targets[state.writeIndex];
        target.BeginCapture(0.015f, 0.02f, 0.035f, 1.0f, 1.0f);
        if (renderSky) {
            SKYRENDERER::Render(
                graphics,
                view.cameraFrame->camera,
                environment,
                modelManager,
                skyManager);
            (void)RENDER3D::PANORAMA_SKY::RenderPanoramaSkyLayer(
                graphics,
                view.cameraFrame->camera,
                environment.sky,
                skyManager);
            target.Rebind();
        }
        const bool rendered = RenderSecondarySceneCapture(
            graphics,
            view,
            environment,
            lighting,
            captureSlot,
            width,
            height,
            renderTransparent);
        target.EndCapture();
        (void)POST::PostSystem::RebindCurrentRenderTarget();
        // Sky/clear is already a valid camera frame. Geometry residency and
        // backend preparation are allowed to recover asynchronously without
        // withholding the texture from the screen material for the complete
        // frame. A capture with no sky still reports the geometry failure.
        if (!rendered && !renderSky) {
            return false;
        }

        ++state.outputRevision;
        if (state.outputRevision == 0u) {
            state.outputRevision = 1u;
        }
        outOutput.texture = state.textures[state.writeIndex];
        outOutput.width = width;
        outOutput.height = height;
        outOutput.outputRevision = state.outputRevision;
        outOutput.ready = true;
        state.writeIndex =
            (state.writeIndex + 1u) %
            static_cast<uint32_t>(state.targets.size());
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
            if (captureSlot <
                    MESHRENDERER::kSecondarySceneCaptureMaximumCount) {
                SecondaryCaptureTargets()[captureSlot].Reset();
            }
        }
        catch (...) {
        }
    }

    void ResetSecondarySceneCaptureTarget() noexcept {
        for (uint32_t captureSlot = 0u;
             captureSlot <
                MESHRENDERER::kSecondarySceneCaptureMaximumCount;
             ++captureSlot) {
            ResetSecondarySceneCaptureTarget(captureSlot);
        }
    }

    const ModelRendererDebugStats& GetDebugStats() {
        SyncRenderModelCacheStats();
        return gDebugStats;
    }

    RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache() {
        return gRenderModelCache;
    }

    const RENDER3D::RUNTIME::RenderModelCache::Stats& GetRenderModelCacheStats() {
        return gRenderModelCache.GetStats();
    }

}
