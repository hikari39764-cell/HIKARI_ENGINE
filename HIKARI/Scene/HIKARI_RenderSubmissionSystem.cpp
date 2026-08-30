#include "Scene/HIKARI_RenderSubmissionSystem.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_FrameContext.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuSceneFrameBuffer.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_TerrainGrassModelFactory.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

#include <utility>
#include <string>

namespace HIKARI {

    RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
    const AssetRegistry* RenderSubmissionSystem::sAssetRegistry_ = nullptr;
    ModelManager* RenderSubmissionSystem::sModelManager_ = nullptr;
    std::filesystem::path RenderSubmissionSystem::sProjectRoot_{};
    RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
    RENDER3D::GPUDRIVEN::GpuSceneRegistry RenderSubmissionSystem::sGpuSceneRegistry_{};
    RENDER3D::GPUDRIVEN::MassGpuSceneRegistry
        RenderSubmissionSystem::sMassGpuSceneRegistry_{};
    RENDER3D::GPUDRIVEN::GpuDrivenSceneSource
        RenderSubmissionSystem::sCombinedGpuSceneSource_{};
    SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
    bool RenderSubmissionSystem::sGpuSceneRegistryValid_ = false;
    bool RenderSubmissionSystem::sGpuDrivenSceneSyncInitialized_ = false;
    uint64_t RenderSubmissionSystem::sSubmissionWorldIdentity_ = 0u;
    uint64_t RenderSubmissionSystem::sPendingMassPublicationGeneration_ = 0u;
    uint64_t RenderSubmissionSystem::sPendingMassPublicationFrameIndex_ = 0u;
    uint64_t RenderSubmissionSystem::sPendingMassSourceVersion_ = 0u;
    RENDER3D::RenderViewPurpose
        RenderSubmissionSystem::sRenderViewPurpose_ =
            RENDER3D::RenderViewPurpose::Game;

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

        void TickPresentationServices(
            World& world,
            const FrameContext& frame) noexcept {
            try {
                // Playback is a world-owned service, not a component-owned
                // slot system. Tick it here so public VfxView users still
                // advance/render Owner or World effects without a
                // VfxPlayerComponent. VfxPlayerSystem may also call Update
                // earlier in this frame; the service de-duplicates the frame
                // token and preserves one deterministic step.
                if (auto* playback = world.Services().Find<
                        VFX::VfxPlaybackService>()) {
                    playback->Update(frame.frameIndex, frame.gameDt);
                }
                if (auto* combat = world.Services().Find<
                        VFX::CombatPresentationService>()) {
                    combat->Tick(frame.frameIndex, frame.unscaledDt);
                }
                if (auto* afterimage = world.Services().Find<
                        VFX::CombatAfterimageService>()) {
                    afterimage->Tick(frame.frameIndex, frame.unscaledDt);
                }
                if (auto* reaction = world.Services().Find<
                        VFX::ActionPostReactionService>()) {
                    reaction->Tick(frame.unscaledDt, frame.frameIndex);
                }
                if (auto* gameUi = world.Services().Find<
                        GAME_UI::GameUiPresentationService>()) {
                    gameUi->Tick(frame.frameIndex);
                    GAME_UI::SetGameUiFrameSource(
                        &gameUi->GetFrameSource());
                }
            }
            catch (...) {
            }
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

        struct ResolvedMassRenderModel {
            const ModelAsset* model = nullptr;
            std::string clusteredGeometryPath{};
        };

        bool ResolveMassRenderModel(
            const GameProjectWorldMassRenderProxyDesc& proxy,
            const AssetRegistry* assetRegistry,
            ModelManager* modelManager,
            const std::filesystem::path& projectRoot,
            ResolvedMassRenderModel& outResolved) {
            outResolved = {};
            ProceduralMeshSettings proceduralSettings{};
            bool procedural = false;
            switch (proxy.geometry) {
            case GameProjectWorldMassRenderGeometry::BuiltInBox:
                proceduralSettings.kind = ProceduralMeshKind::Box;
                procedural = true;
                break;
            case GameProjectWorldMassRenderGeometry::BuiltInSphere:
                proceduralSettings.kind = ProceduralMeshKind::Sphere;
                proceduralSettings.sphereSlices = 12u;
                proceduralSettings.sphereStacks = 8u;
                procedural = true;
                break;
            case GameProjectWorldMassRenderGeometry::ModelAsset:
                break;
            default:
                return false;
            }
            if (procedural) {
                if (PROCEDURAL::TryGetWarmedClusteredGeometry(
                        proceduralSettings,
                        projectRoot,
                        outResolved.model,
                        outResolved.clusteredGeometryPath)) {
                    return outResolved.model != nullptr &&
                        !outResolved.clusteredGeometryPath.empty();
                }
                PROCEDURAL::RequestClusteredGeometryWarmUp(
                    proceduralSettings,
                    projectRoot);
                return false;
            }
            if (assetRegistry == nullptr || modelManager == nullptr ||
                proxy.modelAssetGuid[0] == '\0') {
                return false;
            }
            const std::string assetGuid(proxy.modelAssetGuid);
            const auto* descriptor =
                assetRegistry->FindAs<ModelAssetDescriptor>(
                    AssetId{ assetGuid });
            if (descriptor == nullptr) {
                return false;
            }
            outResolved.model = ResolveModelAssetNow(
                *modelManager,
                *assetRegistry,
                descriptor->id.value);
            if (outResolved.model == nullptr ||
                outResolved.model->GetState() != ModelAsset::State::Loaded ||
                descriptor->clusteredGeometryPath.empty()) {
                outResolved = {};
                return false;
            }
            std::filesystem::path clusterPath =
                descriptor->clusteredGeometryPath;
            if (!clusterPath.is_absolute() && !projectRoot.empty()) {
                clusterPath = projectRoot / clusterPath;
            }
            outResolved.clusteredGeometryPath =
                clusterPath.lexically_normal().generic_string();
            return !outResolved.clusteredGeometryPath.empty();
        }

        struct MassRenderSyncContext {
            RENDER3D::GPUDRIVEN::MassGpuSceneRegistry* registry = nullptr;
            const AssetRegistry* assetRegistry = nullptr;
            ModelManager* modelManager = nullptr;
            RENDER3D::RUNTIME::RenderModelCache* renderModelCache = nullptr;
            const std::filesystem::path* projectRoot = nullptr;
            uint64_t worldIdentity = 0u;
            uint64_t frameIndex = 0u;
        };

        bool SyncMassRenderStores(
            void* rawContext,
            const WorldMassRenderPublicationReceipt& receipt,
            std::span<const WorldMassRenderStoreView> stores) noexcept {
            try {
                auto& context = *static_cast<MassRenderSyncContext*>(
                    rawContext);
                if (context.registry == nullptr ||
                    context.renderModelCache == nullptr ||
                    context.worldIdentity == 0u ||
                    receipt.worldIdentity != context.worldIdentity ||
                    receipt.frameIndex != context.frameIndex ||
                    receipt.generation == 0u) {
                    return false;
                }
                context.registry->BeginSync(
                    context.worldIdentity,
                    context.frameIndex,
                    receipt.generation);
                for (const WorldMassRenderStoreView& store : stores) {
                    if (store.proxy == nullptr) {
                        context.registry->Clear();
                        return false;
                    }
                    ResolvedMassRenderModel resolved{};
                    if (context.projectRoot == nullptr ||
                        !ResolveMassRenderModel(
                        *store.proxy,
                        context.assetRegistry,
                        context.modelManager,
                        *context.projectRoot,
                        resolved) ||
                        !context.registry->AddStore(
                            RENDER3D::GPUDRIVEN::MassGpuSceneStoreInput{
                                {
                                    store.typeId.value,
                                    resolved.model,
                                    store.proxy->meshIndex,
                                    store.proxy->primitiveIndex,
                                    store.proxy->materialIndex,
                                    (store.proxy->flags &
                                        static_cast<uint32_t>(
                                            GameProjectWorldMassRenderFlags::
                                                ReceiveShadow)) != 0u,
                                    resolved.clusteredGeometryPath
                                },
                                store.instances,
                                store.dirtyRanges,
                                store.layoutVersion,
                                store.sourceVersion,
                                store.dirtyBaseSourceVersion
                            })) {
                        context.registry->Clear();
                        return false;
                    }
                }
                return context.registry->EndSync(
                    *context.renderModelCache);
            }
            catch (...) {
                auto& context = *static_cast<MassRenderSyncContext*>(
                    rawContext);
                if (context.registry != nullptr) {
                    context.registry->Clear();
                }
                return false;
            }
        }
    }

    void RenderSubmissionSystem::SetAssetContext(
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        ModelManager* modelManager) {

        std::filesystem::path normalizedProjectRoot = NormalizeProjectRoot(std::move(projectRoot));
        // 初回描画時のパイプライン初期化とプロシージャル既定プリミティブの
        // ウォームアップを、ユーザー操作（プリミティブ作成）から切り離す。
        if (!normalizedProjectRoot.empty()) {
            (void)MESHRENDERER::EnsureReadyForFirstFrame(
                GFX::GetCurrentContext());
            PROCEDURAL::WarmUpDefaultPrimitiveClusters(
                normalizedProjectRoot);
        }
        const bool assetContextChanged = sAssetRegistry_ != assetRegistry;
        const bool modelManagerChanged = sModelManager_ != modelManager;
        const bool projectRootChanged = sProjectRoot_ != normalizedProjectRoot;
        if (!assetContextChanged && !modelManagerChanged &&
            !projectRootChanged) {
            return;
        }

        sAssetRegistry_ = assetRegistry;
        sModelManager_ = modelManager;
        sProjectRoot_ = std::move(normalizedProjectRoot);
        InvalidateSceneResources(projectRootChanged);
    }

    void RenderSubmissionSystem::SetRenderViewPurpose(
        RENDER3D::RenderViewPurpose purpose) {

        if (sRenderViewPurpose_ == purpose) {
            return;
        }
        sRenderViewPurpose_ = purpose;
        InvalidateSubmissionRouting();
    }

    RENDER3D::RenderViewPurpose
        RenderSubmissionSystem::GetRenderViewPurpose() {

        return sRenderViewPurpose_;
    }

    void RenderSubmissionSystem::InvalidateSubmissionRouting() {
        MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
        SHADOW::SetGpuDrivenSceneSource(nullptr);
        sSceneRenderCache_.Clear();
        sSceneRenderCacheSync_.Clear();
        sGpuSceneRegistry_.Clear();
        sMassGpuSceneRegistry_.Clear();
        sCombinedGpuSceneSource_.Reset();
        sGpuSceneRegistryValid_ = false;
        sGpuDrivenSceneSyncInitialized_ = false;
        sSubmissionWorldIdentity_ = 0u;
        sPendingMassPublicationGeneration_ = 0u;
        sPendingMassPublicationFrameIndex_ = 0u;
        sPendingMassSourceVersion_ = 0u;
    }

    void RenderSubmissionSystem::InvalidateSurfaceResourceBindings() {
        InvalidateSubmissionRouting();
    }

    void RenderSubmissionSystem::InvalidateSceneResources(bool clearProceduralCache) {
        MODELRENDERER::GetRenderModelCache().Clear();
        InvalidateSubmissionRouting();
        MESHRENDERER::InvalidateMaterialFxPipelineCache();
        SHADOW::InvalidateSceneCache();
        if (clearProceduralCache) {
            PROCEDURAL::ClearCache();
            TERRAIN::ClearTerrainGrassModelCache();
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
        sDebugStats_.massRenderStoreCount = 0u;
        sDebugStats_.massRenderInstanceCount = 0u;
        sDebugStats_.massRenderDirtyRangeCount = 0u;
        sDebugStats_.massRenderMissingTemplateCount = 0u;
        sDebugStats_.massRenderUnsupportedTemplateCount = 0u;
        sDebugStats_.routeMode = RenderSubmissionRouteMode::GpuDrivenMainline;
        sDebugStats_.gpuDrivenMainRouteActive = true;

        if (sSubmissionWorldIdentity_ != world.GetRuntimeIdentity()) {
            InvalidateSubmissionRouting();
            sSubmissionWorldIdentity_ = world.GetRuntimeIdentity();
        }

        TickPresentationServices(world, frame);

        if (!sGpuDrivenSceneSyncInitialized_) {
            sSceneRenderCacheSync_.Sync(
                world,
                MODELRENDERER::GetRenderModelCache(),
                sSceneRenderCache_,
                frame.frameIndex,
                sAssetRegistry_,
                sProjectRoot_,
                sRenderViewPurpose_,
                sModelManager_);
            sGpuDrivenSceneSyncInitialized_ = true;
        } else {
            sSceneRenderCacheSync_.SyncDirty(
                world,
                MODELRENDERER::GetRenderModelCache(),
                sSceneRenderCache_,
                frame.frameIndex,
                sAssetRegistry_,
                sProjectRoot_,
                sRenderViewPurpose_,
                sModelManager_);
        }

        RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput gpuSceneInput{};
        gpuSceneInput.sceneCache = &sSceneRenderCache_;
        sGpuSceneRegistry_.SyncForwardFromSceneCache(gpuSceneInput);
        sGpuSceneRegistryValid_ = true;

        MassRenderSyncContext massContext{
            &sMassGpuSceneRegistry_,
            sAssetRegistry_,
            sModelManager_,
            &MODELRENDERER::GetRenderModelCache(),
            &sProjectRoot_,
            world.GetRuntimeIdentity(),
            frame.frameIndex
        };
        WorldMassRenderPublicationReceipt massReceipt{};
        const bool massPublished = world.PublishMassRenderProxies(
            frame.frameIndex,
            &massContext,
            &SyncMassRenderStores,
            massReceipt);
        if (massPublished) {
            sPendingMassPublicationGeneration_ = massReceipt.generation;
            sPendingMassPublicationFrameIndex_ = massReceipt.frameIndex;
            sPendingMassSourceVersion_ =
                sMassGpuSceneRegistry_.GetSource().sourceVersion;
        } else {
            sMassGpuSceneRegistry_.Clear();
            sPendingMassPublicationGeneration_ = 0u;
            sPendingMassPublicationFrameIndex_ = 0u;
            sPendingMassSourceVersion_ = 0u;
        }
        const auto& massStats = sMassGpuSceneRegistry_.GetStats();
        sDebugStats_.massRenderStoreCount = massStats.storeCount;
        sDebugStats_.massRenderInstanceCount =
            massStats.sourceInstanceCount;
        sDebugStats_.massRenderDirtyRangeCount =
            massStats.dirtyRangeCount;
        sDebugStats_.massRenderMissingTemplateCount =
            massStats.missingTemplateCount;
        sDebugStats_.massRenderUnsupportedTemplateCount =
            massStats.unsupportedTemplateCount;

        sCombinedGpuSceneSource_ =
            sGpuSceneRegistry_.GetSceneSource();
        sCombinedGpuSceneSource_.mass =
            &sMassGpuSceneRegistry_.GetSource();

        if (sGpuSceneRegistryValid_) {
            MESHRENDERER::SetGpuDrivenSceneSource(
                &sCombinedGpuSceneSource_);
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

        const bool lightingBakeCapture =
            sRenderViewPurpose_ ==
                RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
            sRenderViewPurpose_ ==
                RENDER3D::RenderViewPurpose::LightProbeCapture;
        if (!lightingBakeCapture) {
            SubmitDebugOverlays(world, sDebugStats_);
        }
    }

    void RenderSubmissionSystem::AcknowledgeMassRenderFrame(
        World& world,
        const FrameContext& frame) {
        if (sPendingMassPublicationGeneration_ == 0u) {
            return;
        }
        const auto& stats =
            MESHRENDERER::GetMassGpuSceneFrameBufferStats();
        if (stats.sceneResident &&
            stats.worldIdentity == world.GetRuntimeIdentity() &&
            stats.sourceVersion == sPendingMassSourceVersion_ &&
            stats.publicationFrameIndex ==
                sPendingMassPublicationFrameIndex_ &&
            stats.publicationFrameIndex == frame.frameIndex &&
            stats.publicationGeneration ==
                sPendingMassPublicationGeneration_ &&
            world.AcknowledgeMassRenderProxies(
                WorldMassRenderPublicationReceipt{
                    world.GetRuntimeIdentity(),
                    sPendingMassPublicationGeneration_,
                    sPendingMassPublicationFrameIndex_
                })) {
            sPendingMassPublicationGeneration_ = 0u;
            sPendingMassPublicationFrameIndex_ = 0u;
            sPendingMassSourceVersion_ = 0u;
        }
    }

} // namespace HIKARI
