#include "Scene/HIKARI_PresentationModelRenderProjection.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationSocketService.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingResolver.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PRESENTATION_MODEL {
    namespace {

        const ModelAssetDescriptor* ResolveModelDescriptor(
            const PresentationModelRecord& record,
            const AssetRegistry* assetRegistry,
            const std::filesystem::path& projectRoot) {

            if (assetRegistry == nullptr) {
                return nullptr;
            }
            if (!record.assetGuid.empty()) {
                const ModelAssetDescriptor* byGuid =
                    assetRegistry->FindAs<ModelAssetDescriptor>(
                        AssetId{ record.assetGuid });
                if (byGuid != nullptr || record.assetPath.empty()) {
                    return byGuid;
                }
            }
            if (record.assetPath.empty()) {
                return nullptr;
            }

            std::filesystem::path requestedPath(record.assetPath);
            if (!requestedPath.is_absolute() && !projectRoot.empty()) {
                requestedPath = projectRoot / requestedPath;
            }
            requestedPath = requestedPath.lexically_normal();
            const std::string requestedKey =
                TEXT::ToLowerAsciiCopy(requestedPath.generic_string());
            for (const AssetDescriptor* base : assetRegistry->CollectByType(
                    AssetType::Model)) {
                const auto* descriptor =
                    dynamic_cast<const ModelAssetDescriptor*>(base);
                if (descriptor == nullptr || descriptor->sourcePath.empty()) {
                    continue;
                }
                std::filesystem::path descriptorPath(
                    descriptor->sourcePath);
                if (!descriptorPath.is_absolute() && !projectRoot.empty()) {
                    descriptorPath = projectRoot / descriptorPath;
                }
                const std::string descriptorKey = TEXT::ToLowerAsciiCopy(
                    descriptorPath.lexically_normal().generic_string());
                if (descriptorKey == requestedKey) {
                    return descriptor;
                }
            }
            return nullptr;
        }

        const ModelAsset* ResolveModelAsset(
            const ModelAssetDescriptor* descriptor,
            const AssetRegistry* assetRegistry,
            ModelManager* modelManager) {

            if (modelManager == nullptr || descriptor == nullptr ||
                assetRegistry == nullptr) {
                return nullptr;
            }
            ModelAsset* asset = ResolveModelAssetNow(
                *modelManager,
                *assetRegistry,
                descriptor->id.value);
            return asset != nullptr &&
                    asset->GetState() == ModelAsset::State::Loaded
                ? asset
                : nullptr;
        }

        bool IsFiniteMatrix(const MATH::Mat4& matrix) noexcept {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(matrix.m[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool ResolveWorldMatrix(
            const PresentationModelRecord& record,
            World& world,
            const PresentationTransformService* presentationTransforms,
            const ANIMATION::AnimationPoseService* animationPoses,
            MATH::Mat4& outWorldMatrix) {

            outWorldMatrix = MATH::Mat4::Identity();
            if (!record.attachmentEnabled) {
                outWorldMatrix = record.worldTransform.GetWorldMatrix();
                return IsFiniteMatrix(outWorldMatrix);
            }

            GameObject* owner = world.FindObject(
                RuntimeObjectHandle::FromValue(
                    record.ownerObjectHandle));
            if (owner == nullptr) {
                return false;
            }
            ModelComponent* ownerModel =
                owner->GetComponent<ModelComponent>();
            if (ownerModel == nullptr) {
                return false;
            }
            const ModelAsset* ownerAsset = ResolveRenderableModelAsset(
                *owner,
                *ownerModel);
            if (ownerAsset == nullptr) {
                return false;
            }

            MATH::Mat4 ownerWorld = owner->GetTransform().GetWorldMatrix();
            if (presentationTransforms != nullptr) {
                MATH::Mat4 presentationWorld{};
                if (presentationTransforms->TryGetWorldMatrix(
                        owner->GetRuntimeHandle(),
                        presentationWorld) &&
                    IsFiniteMatrix(presentationWorld)) {
                    ownerWorld = presentationWorld;
                }
            }
            if (!IsFiniteMatrix(ownerWorld)) {
                return false;
            }

            std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> pose{};
            if (animationPoses != nullptr) {
                pose = animationPoses->Find(owner->GetRuntimeHandle());
            }
            ANIMATION::AnimationSocketService sockets{};
            Transform3D socketTransform{};
            if (!sockets.ResolveWorld(
                    *ownerAsset,
                    pose.get(),
                    ownerWorld,
                    record.socketName,
                    socketTransform)) {
                return false;
            }

            outWorldMatrix = socketTransform.GetWorldMatrix() *
                record.localTransform.GetLocalMatrix();
            return IsFiniteMatrix(outWorldMatrix);
        }

        Bounds ResolveLocalBounds(
            const ModelAsset* model,
            const RENDER3D::RUNTIME::RenderModelAsset* renderModel) {

            if (renderModel != nullptr && renderModel->valid &&
                BOUNDS::IsUsable(renderModel->localBounds)) {
                return renderModel->localBounds;
            }
            if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
                return model->bounds;
            }
            return {};
        }

        bool BuildExclusiveNodeVisibilityMask(
            const ModelAsset& model,
            std::string_view nodeName,
            std::vector<uint8_t>& outMask) {
            outMask.clear();
            if (nodeName.empty()) {
                return true;
            }

            // ModelAsset preserves importer order. std::find_if therefore
            // deliberately selects the first exact, case-sensitive match;
            // duplicate node names do not make the result nondeterministic.
            const auto selected = std::find_if(
                model.nodes.begin(),
                model.nodes.end(),
                [nodeName](const ModelNode& node) {
                    return node.name == nodeName;
                });
            if (selected == model.nodes.end()) {
                return false;
            }

            const size_t selectedIndex = static_cast<size_t>(
                selected - model.nodes.begin());
            outMask.assign(model.nodes.size(), uint8_t{ 0u });
            for (size_t nodeIndex = 0u;
                nodeIndex < model.nodes.size();
                ++nodeIndex) {
                size_t current = nodeIndex;
                for (size_t depth = 0u;
                    depth < model.nodes.size();
                    ++depth) {
                    if (current == selectedIndex) {
                        outMask[nodeIndex] = 1u;
                        break;
                    }
                    const int parent = model.nodes[current].parent;
                    if (parent < 0 ||
                        static_cast<size_t>(parent) >= model.nodes.size() ||
                        static_cast<size_t>(parent) == current) {
                        break;
                    }
                    current = static_cast<size_t>(parent);
                }
            }
            return true;
        }

        void ProjectMaterialSet(
            const PresentationModelRecord& record,
            const ModelAsset& asset,
            const AssetRegistry* assetRegistry,
            MaterialStorage& materialStorage,
            std::unordered_set<std::string>& diagnostics) {

            const std::string& modelAssetGuid = asset.id.value;
            const uint64_t assetRegistryInstanceId = assetRegistry != nullptr
                ? assetRegistry->GetInstanceId()
                : 0u;
            const uint64_t assetRegistryRevision = assetRegistry != nullptr
                ? assetRegistry->GetRevision()
                : 0u;
            if (materialStorage.modelAssetGuid == modelAssetGuid &&
                materialStorage.materialSetAssetGuid ==
                    record.materialSetAssetGuid &&
                materialStorage.assetRegistry == assetRegistry &&
                materialStorage.assetRegistryInstanceId ==
                    assetRegistryInstanceId &&
                materialStorage.assetRegistryRevision ==
                    assetRegistryRevision) {
                return;
            }
            materialStorage.modelAssetGuid = modelAssetGuid;
            materialStorage.materialSetAssetGuid =
                record.materialSetAssetGuid;
            materialStorage.assetRegistry = assetRegistry;
            materialStorage.assetRegistryInstanceId =
                assetRegistryInstanceId;
            materialStorage.assetRegistryRevision = assetRegistryRevision;
            materialStorage.owned.clear();
            materialStorage.overrides.clear();

            if (assetRegistry == nullptr) {
                return;
            }

            const AssetGuid materialSetAssetGuid{
                record.materialSetAssetGuid };
            const auto resolution =
                ASSETS::MODELS::ResolveModelMaterialBindings(
                    *assetRegistry,
                    AssetGuid{ asset.id.value },
                    materialSetAssetGuid,
                    asset.materials,
                    !materialSetAssetGuid.IsValid());
            for (const std::string& diagnostic : resolution.diagnostics) {
                const std::string key = asset.id.value + "|" +
                    record.materialSetAssetGuid + "|" + diagnostic;
                if (diagnostics.insert(key).second) {
                    HIKARI_LOG_WARN(
                        "[PresentationModel][MaterialSet] " + diagnostic);
                }
            }

            materialStorage.owned.reserve(resolution.bindings.size());
            materialStorage.overrides.reserve(resolution.bindings.size());
            for (const auto& binding : resolution.bindings) {
                if (binding.materialDescriptor == nullptr) {
                    continue;
                }
                std::unique_ptr<Material> material =
                    BuildRuntimeMaterialOverride(
                        *assetRegistry,
                        *binding.materialDescriptor,
                        true);
                if (material == nullptr) {
                    const std::string key = asset.id.value + "|" +
                        record.materialSetAssetGuid + "|material-build|" +
                        binding.materialAssetGuid.value;
                    if (diagnostics.insert(key).second) {
                        HIKARI_LOG_WARN(
                            "[PresentationModel][MaterialSet] runtime "
                            "material build failed: " +
                            binding.materialAssetGuid.value);
                    }
                    continue;
                }
                const Material* materialPtr = material.get();
                const uint64_t materialRevision = materialPtr->GetRevision();
                materialStorage.owned.push_back(std::move(material));
                materialStorage.overrides.push_back(
                    RENDER3D::RUNTIME::SceneRenderMaterialOverride{
                        binding.materialIndex,
                        materialPtr,
                        materialRevision
                    });
            }
            std::sort(
                materialStorage.overrides.begin(),
                materialStorage.overrides.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.materialIndex < rhs.materialIndex;
                });
        }

        RENDER3D::RUNTIME::SceneRenderObjectDesc BuildRenderObjectDesc(
            const PresentationModelRecord& record,
            const ModelAsset* asset,
            const ModelAssetDescriptor& descriptor,
            const std::filesystem::path& projectRoot,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            World& world,
            const AssetRegistry* assetRegistry,
            const PresentationTransformService* presentationTransforms,
            const ANIMATION::AnimationPoseService* animationPoses,
            MaterialStorage& materialStorage,
            std::unordered_set<std::string>& missingNodeDiagnostics,
            std::unordered_set<std::string>& attachmentDiagnostics,
            std::unordered_set<std::string>& materialDiagnostics) {

            RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
            desc.id = { record.renderObjectId };
            desc.model = asset;
            if (asset != nullptr) {
                desc.renderModel = renderModelCache.GetOrCreate(*asset);
            }
            if (!descriptor.clusteredGeometryPath.empty()) {
                std::filesystem::path path = descriptor.clusteredGeometryPath;
                if (!path.is_absolute() && !projectRoot.empty()) {
                    path = projectRoot / path;
                }
                desc.clusteredGeometryPath =
                    path.lexically_normal().generic_string();
            }

            MATH::Mat4 worldMatrix = MATH::Mat4::Identity();
            const bool transformResolved = ResolveWorldMatrix(
                record,
                world,
                presentationTransforms,
                animationPoses,
                worldMatrix);
            if (transformResolved) {
                desc.worldTransform.useExplicitMatrix = true;
                desc.worldTransform.explicitMatrix = worldMatrix;
            }
            desc.localBounds = ResolveLocalBounds(
                desc.model,
                desc.renderModel);
            desc.worldBounds = BOUNDS::TransformBounds(
                desc.localBounds,
                worldMatrix);
            bool exclusiveNodeResolved = true;
            if (asset != nullptr && !record.exclusiveNodeName.empty()) {
                exclusiveNodeResolved = BuildExclusiveNodeVisibilityMask(
                    *asset,
                    record.exclusiveNodeName,
                    desc.nodeVisibilityMask);
                if (!exclusiveNodeResolved) {
                    const std::string modelIdentity =
                        !asset->id.value.empty()
                        ? asset->id.value
                        : asset->sourcePath;
                    const std::string diagnosticKey = modelIdentity + "|" +
                        record.exclusiveNodeName;
                    if (missingNodeDiagnostics.insert(diagnosticKey).second) {
                        HIKARI_LOG_WARN(
                            "[PresentationModel] exclusive node unavailable: " +
                            record.exclusiveNodeName + " in model " +
                            modelIdentity);
                    }
                }
            }
            desc.visible = record.visible &&
                transformResolved &&
                desc.model != nullptr &&
                desc.renderModel != nullptr &&
                desc.renderModel->valid &&
                exclusiveNodeResolved;
            desc.isStatic = false;
            desc.castShadow = record.castShadow;
            desc.receiveShadow = record.receiveShadow;
            desc.hasRuntimeAnimation = false;
            desc.allowStaticCachedForward = false;
            desc.materialFxProfileId =
                record.materialFx.GetProfileId();
            desc.materialFxValuesInitialized =
                record.materialFx.AreValuesInitialized();
            const auto& materialFxValues =
                record.materialFx.GetValues();
            for (size_t index = 0u;
                index < VFX::kMaterialFxUserCount;
                ++index) {
                desc.materialFxParamValues[index] =
                    materialFxValues[index];
            }

            if (!transformResolved && record.attachmentEnabled) {
                const std::string key =
                    std::to_string(record.ownerObjectHandle) + "|" +
                    record.socketName;
                if (attachmentDiagnostics.insert(key).second) {
                    HIKARI_LOG_WARN(
                        "[PresentationModel] attachment socket unavailable: " +
                        record.socketName);
                }
            }
            if (asset != nullptr) {
                ProjectMaterialSet(
                    record,
                    *asset,
                    assetRegistry,
                    materialStorage,
                    materialDiagnostics);
                desc.materialOverrides = materialStorage.overrides;
            }
            return desc;
        }
    }

    void SyncRenderObjects(
        World& world,
        const PresentationModelService* presentationModels,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        const AssetRegistry* assetRegistry,
        ModelManager* modelManager,
        const std::filesystem::path& projectRoot,
        const PresentationTransformService* presentationTransforms,
        const ANIMATION::AnimationPoseService* animationPoses,
        MaterialStorageByRenderObject& materialStorage,
        std::unordered_set<std::string>& missingAssetDiagnostics,
        std::unordered_set<std::string>& missingNodeDiagnostics,
        std::unordered_set<std::string>& attachmentDiagnostics,
        std::unordered_set<std::string>& materialDiagnostics) {

        if (presentationModels == nullptr) {
            materialStorage.clear();
            return;
        }
        for (const PresentationModelRecord& record :
                presentationModels->GetFrameRecords()) {
            const ModelAssetDescriptor* descriptor = ResolveModelDescriptor(
                record,
                assetRegistry,
                projectRoot);
            const ModelAsset* asset = ResolveModelAsset(
                descriptor,
                assetRegistry,
                modelManager);
            if (asset == nullptr) {
                const std::string diagnosticKey =
                    !record.assetGuid.empty()
                    ? record.assetGuid
                    : record.assetPath;
                if (missingAssetDiagnostics.insert(diagnosticKey).second) {
                    HIKARI_LOG_WARN(
                        "[PresentationModel] model asset unavailable: " +
                        diagnosticKey);
                }
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{
                        record.renderObjectId });
                materialStorage.erase(record.renderObjectId);
                continue;
            }
            auto& recordMaterials = materialStorage[record.renderObjectId];
            sceneRenderCache.Upsert(BuildRenderObjectDesc(
                record,
                asset,
                *descriptor,
                projectRoot,
                renderModelCache,
                world,
                assetRegistry,
                presentationTransforms,
                animationPoses,
                recordMaterials,
                missingNodeDiagnostics,
                attachmentDiagnostics,
                materialDiagnostics));
        }
    }
} // namespace HIKARI::PRESENTATION_MODEL
