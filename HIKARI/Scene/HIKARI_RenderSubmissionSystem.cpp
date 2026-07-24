#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_FrameContext.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

#include <utility>

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
    const AssetRegistry* RenderSubmissionSystem::sAssetRegistry_ = nullptr;
    std::filesystem::path RenderSubmissionSystem::sProjectRoot_{};
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::GPUDRIVEN::GpuSceneRegistry RenderSubmissionSystem::sGpuSceneRegistry_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    bool RenderSubmissionSystem::sGpuSceneRegistryValid_ = false;
    bool RenderSubmissionSystem::sGpuDrivenSceneSyncInitialized_ = false;

    const char* ToString(RenderSubmissionRouteMode mode) {
        switch (mode) {
        case RenderSubmissionRouteMode::GpuDrivenMainline:
            return "GPU Driven Mainline";
        default:
            return "Unknown";
        }
    }

    namespace {
        std::filesystem::path NormalizeProjectRoot(std::filesystem::path projectRoot) {
            return projectRoot.empty() ? std::filesystem::path{} : projectRoot.lexically_normal();
        }

        void SubmitDebugOverlays(World& world, RenderSubmissionDebugStats& stats) {
            MESHWIREDEBUG::BeginFrame();
            world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
                if (!model.IsVisible()) {
                    return;
                }

                const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
                if (debugMode == ModelRenderDebugMode::Normal) {
                    return;
                }

                const ModelAsset* asset =
                    ResolveRenderableModelAsset(object, model);
                if (asset == nullptr) {
                    return;
                }

                const Transform3D& worldTransform = object.GetTransform();
                if (debugMode == ModelRenderDebugMode::WireOverlay ||
                    debugMode == ModelRenderDebugMode::WireOnly) {
                    MESHWIREDEBUG::SubmitModelWire(
                        *asset,
                        worldTransform,
                        model.GetWireColor(),
                        model.GetMaxWireLines(),
                        model.GetWirePerPrimitiveColor());
                    ++stats.fallbackWireCount;
                } else if (debugMode == ModelRenderDebugMode::BoundsOnly) {
                    MESHWIREDEBUG::SubmitModelBounds(
                        *asset,
                        worldTransform,
                        model.GetWireColor());
                    ++stats.fallbackWireCount;
                }
            });
        }
    }

    void RenderSubmissionSystem::SetAssetContext(
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot) {

        std::filesystem::path normalizedProjectRoot = NormalizeProjectRoot(std::move(projectRoot));
        const bool assetContextChanged = sAssetRegistry_ != assetRegistry;
        const bool projectRootChanged = sProjectRoot_ != normalizedProjectRoot;
        if (!assetContextChanged && !projectRootChanged) {
            return;
        }

        sAssetRegistry_ = assetRegistry;
        sProjectRoot_ = std::move(normalizedProjectRoot);
        InvalidateSceneResources(projectRootChanged);
    }

    void RenderSubmissionSystem::InvalidateSceneResources(bool clearProceduralCache) {
        MODELRENDERER::GetRenderModelCache().Clear();
        sSceneRenderCache_.Clear();
        sGpuSceneRegistry_.Clear();
        sGpuSceneRegistryValid_ = false;
        sGpuDrivenSceneSyncInitialized_ = false;
        MESHRENDERER::InvalidateMaterialFxPipelineCache();
        MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
        SHADOW::InvalidateSceneCache();
        SHADOW::SetGpuDrivenSceneSource(nullptr);
        if (clearProceduralCache) {
            PROCEDURAL::ClearCache();
        }
    }

    RenderSubmissionRouteMode RenderSubmissionSystem::GetRouteMode() {
        return RenderSubmissionRouteMode::GpuDrivenMainline;
    }

    const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() {
        return sDebugStats_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache& RenderSubmissionSystem::GetSceneRenderCache() {
        return sSceneRenderCache_;
    }

    const RENDER3D::RUNTIME::SceneRenderCache::Stats& RenderSubmissionSystem::GetSceneRenderCacheStats() {
        return sSceneRenderCache_.GetStats();
    }

    const RENDER3D::GPUDRIVEN::GpuSceneRegistryStats& RenderSubmissionSystem::GetGpuSceneRegistryStats() {
        return sGpuSceneRegistry_.GetStats();
    }

    void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::RenderSubmission);
        sDebugStats_.submittedModelCount = 0;
        sDebugStats_.scannedModelCount = 0;
        sDebugStats_.hiddenModelCount = 0;
        sDebugStats_.culledModelCount = 0;
        sDebugStats_.missingBoundsCount = 0;
        sDebugStats_.skinnedCullSkippedCount = 0;
        sDebugStats_.fallbackWireCount = 0;
        sDebugStats_.gpuDrivenForwardBypassCount = 0;
        sDebugStats_.gpuDrivenShadowBypassCount = 0;
        sDebugStats_.runtimeSpecialModelCount = 0;
        sDebugStats_.runtimeSpecialForwardModelCount = 0;
        sDebugStats_.runtimeSpecialShadowModelCount = 0;
        sDebugStats_.routeMode = RenderSubmissionRouteMode::GpuDrivenMainline;
        sDebugStats_.gpuDrivenMainRouteActive = true;

        if (!sGpuDrivenSceneSyncInitialized_) {
            sSceneRenderCacheSync_.Sync(
                world,
                MODELRENDERER::GetRenderModelCache(),
                sSceneRenderCache_,
                frame.frameIndex,
                sAssetRegistry_,
                sProjectRoot_);
            sGpuDrivenSceneSyncInitialized_ = true;
        } else {
            sSceneRenderCacheSync_.SyncDirty(
                world,
                MODELRENDERER::GetRenderModelCache(),
                sSceneRenderCache_,
                frame.frameIndex,
                sAssetRegistry_,
                sProjectRoot_);
        }

        RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput gpuSceneInput{};
        gpuSceneInput.sceneCache = &sSceneRenderCache_;
        sGpuSceneRegistry_.SyncForwardFromSceneCache(gpuSceneInput);
        sGpuSceneRegistryValid_ = true;

        if (sGpuSceneRegistryValid_) {
            MESHRENDERER::SetGpuDrivenSceneSource(
                &sGpuSceneRegistry_.GetSceneSource());
        } else {
            MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
        }
        if (sGpuSceneRegistryValid_ &&
            sGpuSceneRegistry_.HasShadowPassSource()) {

            SHADOW::SetGpuDrivenSceneSource(
                &sGpuSceneRegistry_.GetSceneSource());
        } else {
            SHADOW::SetGpuDrivenSceneSource(nullptr);
        }

        SubmitDebugOverlays(world, sDebugStats_);
    }

} // namespace HIKARI
