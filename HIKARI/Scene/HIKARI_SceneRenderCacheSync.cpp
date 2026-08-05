#include "Scene/HIKARI_SceneRenderCacheSync.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
            RENDER3D::RUNTIME::SceneRenderObjectId id{ object.GetDocumentId().value };
            if (!id.IsValid()) {
                id.value = reinterpret_cast<uint64_t>(&object);
            }
            return id;
        }

        std::string ResolveClusteredGeometryPath(
            const GameObject& object,
            const ModelComponent& model,
            const AssetRegistry* assetRegistry,
            const std::filesystem::path& projectRoot) {

            if (const auto* procedural =
                    object.GetComponent<ProceduralMeshComponent>()) {
                return PROCEDURAL::GetOrCreateClusteredGeometryPath(
                    procedural->GetSettings(),
                    projectRoot);
            }

            if (assetRegistry == nullptr ||
                model.GetAssetId().empty()) {
                return {};
            }

            const auto* descriptor =
                assetRegistry->FindAs<ModelAssetDescriptor>(AssetId{ model.GetAssetId() });
            if (descriptor == nullptr || descriptor->clusteredGeometryPath.empty()) {
                return {};
            }

            std::filesystem::path path = descriptor->clusteredGeometryPath;
            if (!path.is_absolute() && !projectRoot.empty()) {
                path = projectRoot / path;
            }
            return path.lexically_normal().generic_string();
        }

        Bounds ResolveLocalBounds(
            const ModelAsset* model,
            const RENDER3D::RUNTIME::RenderModelAsset* renderModel) {

            if (renderModel != nullptr && renderModel->valid && BOUNDS::IsUsable(renderModel->localBounds)) {
                return renderModel->localBounds;
            }
            if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
                return model->bounds;
            }
            return {};
        }

        bool HasRuntimeMaterialRevisionChange(
            const RENDER3D::RUNTIME::SceneRenderObjectDesc& cached,
            const ModelComponent& model) {

            size_t liveMaterialCount = 0u;
            for (const RuntimeMaterialOverrideSlot& slot :
                model.GetRuntimeMaterialOverrides()) {

                const Material* material = slot.material.get();
                if (material == nullptr) {
                    continue;
                }
                ++liveMaterialCount;
                const auto found = std::find_if(
                    cached.materialOverrides.begin(),
                    cached.materialOverrides.end(),
                    [&slot](
                        const RENDER3D::RUNTIME::
                            SceneRenderMaterialOverride& cachedSlot) {
                        return cachedSlot.materialIndex == slot.materialIndex;
                    });
                if (found == cached.materialOverrides.end() ||
                    found->material != material ||
                    found->materialRevision != material->GetRevision()) {
                    return true;
                }
            }
            return liveMaterialCount != cached.materialOverrides.size();
        }

        class ClusteredGeometryPathResolver {
        public:
            ClusteredGeometryPathResolver(
                const AssetRegistry* assetRegistry,
                std::filesystem::path projectRoot)
                : assetRegistry_(assetRegistry)
                , projectRoot_(std::move(projectRoot)) {
            }

            std::string Resolve(
                const GameObject& object,
                const ModelComponent& model) {
                if (object.GetComponent<ProceduralMeshComponent>() != nullptr) {
                    return ResolveClusteredGeometryPath(
                        object, model, assetRegistry_, projectRoot_);
                }
                if (model.GetAssetId().empty()) {
                    return {};
                }
                const std::string& assetId = model.GetAssetId();
                const auto found = pathByAssetId_.find(assetId);
                if (found != pathByAssetId_.end()) {
                    return found->second;
                }

                std::string path = ResolveClusteredGeometryPath(
                    object, model, assetRegistry_, projectRoot_);
                pathByAssetId_.emplace(assetId, path);
                return path;
            }

        private:
            const AssetRegistry* assetRegistry_ = nullptr;
            std::filesystem::path projectRoot_{};
            std::unordered_map<std::string, std::string> pathByAssetId_{};
        };

        RENDER3D::RUNTIME::SceneRenderObjectDesc BuildSceneRenderObjectDesc(
            const GameObject& object,
            ModelComponent& model,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            ClusteredGeometryPathResolver& clusteredGeometryPaths,
            const PresentationTransformService* presentationTransforms,
            const ANIMATION::AnimationPoseService* animationPoses,
            RENDER3D::RenderViewPurpose purpose) {

            RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
            desc.id = ResolveSceneRenderObjectId(object);
            const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
            const bool debugOnly =
                debugMode == ModelRenderDebugMode::WireOnly ||
                debugMode == ModelRenderDebugMode::BoundsOnly;
            const bool lightingBakeCapture =
                purpose ==
                    RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
                purpose ==
                    RENDER3D::RenderViewPurpose::LightProbeCapture;
            desc.visible =
                model.IsVisible() &&
                !debugOnly &&
                (!lightingBakeCapture ||
                    model.GetIncludeInLightingBake());

            const ModelAsset* asset =
                ResolveRenderableModelAsset(object, model);
            desc.model = asset;
            if (asset != nullptr) {
                desc.renderModel = renderModelCache.GetOrCreate(*asset);
            }

            desc.worldTransform = object.GetTransform();
            MATH::Mat4 presentationWorld{};
            if (presentationTransforms != nullptr &&
                presentationTransforms->TryGetWorldMatrix(
                    object.GetRuntimeHandle(),
                    presentationWorld)) {
                desc.worldTransform.useExplicitMatrix = true;
                desc.worldTransform.explicitMatrix = presentationWorld;
            }
            desc.localBounds = ResolveLocalBounds(desc.model, desc.renderModel);
            desc.worldBounds = BOUNDS::TransformBounds(
                desc.localBounds,
                desc.worldTransform.GetWorldMatrix());

            desc.isStatic = model.IsRenderStatic();
            desc.clusteredGeometryPath = clusteredGeometryPaths.Resolve(
                object, model);
            desc.castShadow = model.GetCastShadow();
            desc.receiveShadow = model.GetReceiveShadow();
            if (animationPoses != nullptr) {
                desc.animationPose = animationPoses->Find(
                    object.GetRuntimeHandle());
                desc.hasRuntimeAnimation =
                    desc.animationPose != nullptr &&
                    desc.animationPose->valid &&
                    desc.animationPose->localPose.IsValidFor(
                        asset != nullptr
                            ? asset->nodes.size()
                            : 0u);
                desc.animationPoseRevision =
                    desc.animationPose != nullptr
                        ? desc.animationPose->revision
                        : 0u;
            }
            desc.hasSpecialRenderDebug = false;
            desc.allowStaticCachedForward =
                !desc.hasRuntimeAnimation;
            for (const RuntimeMaterialOverrideSlot& slot :
                model.GetRuntimeMaterialOverrides()) {

                const Material* runtimeMaterial = slot.material.get();
                if (runtimeMaterial == nullptr) {
                    continue;
                }
                desc.materialOverrides.push_back(
                    RENDER3D::RUNTIME::SceneRenderMaterialOverride{
                        slot.materialIndex,
                        runtimeMaterial,
                        runtimeMaterial->GetRevision()
                    });
            }
            std::sort(
                desc.materialOverrides.begin(),
                desc.materialOverrides.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.materialIndex < rhs.materialIndex;
                });
            desc.postGroupMask = model.GetPostGroupMask();
            if (const MaterialFxComponent* materialFx =
                object.GetComponent<MaterialFxComponent>()) {

                desc.materialFxProfileId =
                    materialFx->GetProfileId();
                desc.materialFxValuesInitialized =
                    materialFx->AreValuesInitialized();
                for (size_t i = 0;
                    i < VFX::kMaterialFxUserCount;
                    ++i) {

                    desc.materialFxParamValues[i] =
                        materialFx->GetParamValues()[i];
                }
            }
            return desc;
        }
    }

    void SceneRenderCacheSync::Sync(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose) {

        sceneRenderCache.BeginSync(frameIndex);
        ClusteredGeometryPathResolver clusteredGeometryPaths(
            assetRegistry,
            std::move(projectRoot));
        const PresentationTransformService* presentationTransforms =
            world.Services().Find<PresentationTransformService>();
        const ANIMATION::AnimationPoseService* animationPoses =
            world.Services().Find<ANIMATION::AnimationPoseService>();

        world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                object,
                model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                purpose));
        });

        sceneRenderCache.EndSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeAllRenderObjects();
    }

    void SceneRenderCacheSync::SyncDirty(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose) {

        sceneRenderCache.BeginPatchSync(frameIndex);
        for (const uint64_t removedObjectId : world.GetRemovedRenderObjectIds()) {
            sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{ removedObjectId });
        }

        ClusteredGeometryPathResolver clusteredGeometryPaths(
            assetRegistry,
            std::move(projectRoot));
        const PresentationTransformService* presentationTransforms =
            world.Services().Find<PresentationTransformService>();
        const ANIMATION::AnimationPoseService* animationPoses =
            world.Services().Find<ANIMATION::AnimationPoseService>();

        for (GameObject* object : world.GetRenderDirtyObjects()) {
            if (object == nullptr) {
                continue;
            }

            ModelComponent* model = object->GetComponent<ModelComponent>();
            if (model == nullptr) {
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{ object->GetRenderStableId() });
                continue;
            }

            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                *object,
                *model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                purpose));
        }

        // Material assets publish into the existing runtime Material object.
        // That revision change does not inherently dirty the owning GameObject,
        // so observe it explicitly before the SceneRenderCache snapshot becomes
        // the authoritative GPU Scene source for this frame.
        world.ForEachObjectWith<ModelComponent>(
            [&](GameObject& object, ModelComponent& model) {
                const RENDER3D::RUNTIME::SceneRenderObjectId objectId =
                    ResolveSceneRenderObjectId(object);
                const RENDER3D::RUNTIME::SceneRenderObject* cached =
                    sceneRenderCache.Find(objectId);
                if (cached != nullptr &&
                    !HasRuntimeMaterialRevisionChange(cached->desc, model)) {
                    return;
                }
                sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                    object,
                    model,
                    renderModelCache,
                    clusteredGeometryPaths,
                    presentationTransforms,
                    animationPoses,
                    purpose));
            });

        sceneRenderCache.EndPatchSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeRenderDirtyObjects();
    }

}
