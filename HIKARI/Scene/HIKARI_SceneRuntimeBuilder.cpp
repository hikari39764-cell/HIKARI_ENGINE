#include "HIKARI_SceneRuntimeBuilder.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <numbers>
#include <unordered_set>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelAssetLookup.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingResolver.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Lighting/HIKARI_LightingRuntimeLoader.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntimeConversion.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceMaterialBinder.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        struct RuntimeClusterGeometryLoadRequest {
            std::filesystem::path path{};
        };

        struct RuntimeAssetLoadPlan {
            std::vector<std::string> modelAssetIds{};
            std::vector<RENDER3D::TextureResourceLoadRequest> textureRequests{};
            std::vector<RuntimeClusterGeometryLoadRequest> clusterGeometryRequests{};
            std::unordered_set<std::string> textureKeys{};
            std::unordered_set<std::string> clusterGeometryKeys{};
        };

        std::string NormalizePlanPathKey(std::string value) {
            std::replace(value.begin(), value.end(), '\\', '/');
            TEXT::ToLowerAsciiInPlace(value);
            return value;
        }

        const std::string& SelectRuntimeTexturePath(const TextureAsset3D& texture) {
            return texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
        }

        void AddTextureRequest(
            RuntimeAssetLoadPlan& plan,
            std::string name,
            std::string path,
            RENDER3D::TextureResourceColorSpace colorSpace) {

            if (path.empty()) {
                return;
            }

            const std::string key =
                NormalizePlanPathKey(path) + "|" +
                std::to_string(static_cast<int>(colorSpace));
            if (!plan.textureKeys.insert(key).second) {
                return;
            }

            plan.textureRequests.push_back(RENDER3D::TextureResourceLoadRequest{
                std::move(name),
                std::move(path),
                colorSpace });
        }

        void AddModelTextureSlot(
            RuntimeAssetLoadPlan& plan,
            const ModelAsset& asset,
            const MaterialAsset& material,
            const TextureSlot& slot,
            MaterialTextureUsage usage) {

            const TextureAsset3D* texture =
                ASSETS::MODELS::FindModelTexture(asset, slot);
            if (texture == nullptr) {
                return;
            }

            const std::string& path = SelectRuntimeTexturePath(*texture);
            AddTextureRequest(
                plan,
                "runtime_model/" + asset.GetName() + "/" + material.name + "/" + MaterialTextureUsageName(usage),
                path,
                MaterialTextureColorSpace(usage));
        }

        void CollectModelTextureRequests(
            RuntimeAssetLoadPlan& plan,
            const ModelAsset& asset) {

            for (const MaterialAsset& material : asset.materials) {
                AddModelTextureSlot(plan, asset, material, material.baseColorTexture, MaterialTextureUsage::BaseColor);
                AddModelTextureSlot(plan, asset, material, material.normalTexture, MaterialTextureUsage::Normal);
                AddModelTextureSlot(plan, asset, material, material.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness);
                AddModelTextureSlot(plan, asset, material, material.occlusionTexture, MaterialTextureUsage::Occlusion);
                AddModelTextureSlot(plan, asset, material, material.emissiveTexture, MaterialTextureUsage::Emissive);
                AddModelTextureSlot(plan, asset, material, material.specularTexture, MaterialTextureUsage::Specular);
                AddModelTextureSlot(plan, asset, material, material.specularColorTexture, MaterialTextureUsage::SpecularColor);
            }
        }

        void AddMaterialTextureSlot(
            RuntimeAssetLoadPlan& plan,
            const AssetRegistry& assetRegistry,
            const std::string& materialId,
            const MaterialTextureSlotData& slot,
            MaterialTextureUsage usage) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid()) {
                return;
            }

            const auto* texture = assetRegistry.FindAs<TextureAssetDescriptor>(
                AssetId{ slot.textureAssetGuid.value });
            if (texture == nullptr || texture->sourcePath.empty()) {
                return;
            }

            AddTextureRequest(
                plan,
                "runtime_material/" + materialId + "/" + MaterialTextureUsageName(usage),
                texture->sourcePath,
                MaterialTextureColorSpace(usage));
        }

        void CollectMaterialTextureRequests(
            RuntimeAssetLoadPlan& plan,
            const AssetRegistry& assetRegistry,
            const MaterialAssetDescriptor& descriptor) {

            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.baseColorTexture, MaterialTextureUsage::BaseColor);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.normalTexture, MaterialTextureUsage::Normal);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.metallicRoughnessTexture, MaterialTextureUsage::MetallicRoughness);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.occlusionTexture, MaterialTextureUsage::Occlusion);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.emissiveTexture, MaterialTextureUsage::Emissive);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.specularTexture, MaterialTextureUsage::Specular);
            AddMaterialTextureSlot(plan, assetRegistry, descriptor.id.value, descriptor.data.specularColorTexture, MaterialTextureUsage::SpecularColor);
        }

        void AddClusterGeometryRequest(
            RuntimeAssetLoadPlan& plan,
            const std::filesystem::path& projectRoot,
            const ModelAssetDescriptor& descriptor) {

            if (descriptor.clusteredGeometryPath.empty()) {
                return;
            }

            const std::filesystem::path resolvedPath =
                PROJECT_PATHS::ResolveProjectPath(projectRoot, descriptor.clusteredGeometryPath);
            const std::string renderPath = resolvedPath.generic_string();
            const std::string sourceKey =
                RENDER3D::BuildClusterGeometryResourceSourceKey(
                    resolvedPath);
            if (sourceKey.empty()) {
                return;
            }

            if (!plan.clusterGeometryKeys.insert(sourceKey).second) {
                return;
            }

            plan.clusterGeometryRequests.push_back(RuntimeClusterGeometryLoadRequest{
                resolvedPath });
        }

        void ExecuteRuntimeAssetLoadPlan(RuntimeAssetLoadPlan& plan) {
            if (!plan.textureRequests.empty()) {
                RENDER3D::TextureResourceBatchLoadStats textureStats{};
                RENDER3D::PreloadTextureResourcesWithColorSpace(
                    plan.textureRequests,
                    &textureStats);

                HIKARI_LOG_INFO(
                    "[SceneRuntimeBuilder][LoadPlan][Texture] requested=" + std::to_string(textureStats.requested) +
                    " cacheHits=" + std::to_string(textureStats.cacheHits) +
                    " uploaded=" + std::to_string(textureStats.uploaded) +
                    " registered=" + std::to_string(textureStats.registered) +
                    " failed=" + std::to_string(textureStats.failed) +
                    " uploadBytes=" + std::to_string(textureStats.uploadedBytes));
            }

            uint32_t clusterLoaded = 0;
            uint32_t clusterFailed = 0;
            for (const RuntimeClusterGeometryLoadRequest& request : plan.clusterGeometryRequests) {
                const RENDER3D::ClusterGeometryResourceHandle handle =
                    RENDER3D::LoadClusterGeometryResource(request.path);
                if (handle) {
                    ++clusterLoaded;
                } else {
                    ++clusterFailed;
                }
            }
            if (!plan.clusterGeometryRequests.empty()) {
                HIKARI_LOG_INFO(
                    "[SceneRuntimeBuilder][LoadPlan][ClusterGeometry] requested=" +
                    std::to_string(plan.clusterGeometryRequests.size()) +
                    " loaded=" + std::to_string(clusterLoaded) +
                    " failed=" + std::to_string(clusterFailed));
            }
        }

        void EnsureModelLoaded(ModelManager& modelManager, const ModelAssetDescriptor& descriptor) {
            ModelAsset* existing = modelManager.FindAsset(descriptor.id.value);
            if (existing != nullptr) {
                if (existing->GetSourcePath() == descriptor.sourcePath && existing->GetState() == ModelAsset::State::Loaded) {
                    return;
                }

                if (existing->GetSourcePath() == descriptor.sourcePath && existing->GetState() == ModelAsset::State::Unloaded) {
                    modelManager.LoadAssetNow(descriptor.id.value);
                    return;
                }
            }

            modelManager.RegisterAsset(descriptor.id.value, descriptor.sourcePath);
            modelManager.LoadAssetNow(descriptor.id.value);
        }

        ModelAsset* ResolveModelAssetNowInternal(
            ModelManager& modelManager,
            const AssetRegistry& assetRegistry,
            const std::string& assetId) {

            if (assetId.empty()) {
                return nullptr;
            }

            const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ assetId });
            if (descriptor != nullptr) {
                EnsureModelLoaded(modelManager, *descriptor);
                return modelManager.FindAsset(assetId);
            }

            ModelAsset* existing = modelManager.FindAsset(assetId);
            if (existing != nullptr && existing->GetState() == ModelAsset::State::Unloaded) {
                modelManager.LoadAssetNow(assetId);
            }
            return modelManager.FindAsset(assetId);
        }

        void BuildModelMaterialOverrides(
            ModelComponent& modelComponent,
            const AssetRegistry& assetRegistry,
            const ModelAsset* modelAsset,
            const std::string* proceduralMaterialSelection) {

            modelComponent.ClearRuntimeMaterialOverrides();
            if (modelAsset == nullptr) {
                return;
            }

            const auto applyMaterial = [&modelComponent,
                &assetRegistry](
                    uint32_t materialIndex,
                    const MaterialAssetDescriptor& descriptor,
                    bool inheritModelTextures) {
                auto runtimeMaterial = BuildRuntimeMaterialOverride(
                    assetRegistry,
                    descriptor,
                    inheritModelTextures);
                if (runtimeMaterial == nullptr) {
                    return false;
                }
                modelComponent.SetRuntimeMaterialOverride(
                    materialIndex,
                    std::move(runtimeMaterial));
                return true;
            };

            // プロシージャル形状は ProceduralMeshComponent がマテリアル選択を
            // 所有する。ModelComponent 側の既定/上書きは適用しない。
            if (proceduralMaterialSelection != nullptr) {
                if (!proceduralMaterialSelection->empty()) {
                    const auto* descriptor =
                        assetRegistry.FindAs<MaterialAssetDescriptor>(
                            AssetId{ *proceduralMaterialSelection });
                    if (descriptor != nullptr) {
                        (void)applyMaterial(0, *descriptor, false);
                    } else {
                        HIKARI_LOG_WARN(
                            "[MaterialRuntime][Procedural] material "
                            "selection not registered: " +
                            *proceduralMaterialSelection);
                    }
                }
                return;
            }

            // imported -> explicit model-owned Material Set (legacy scenes
            // without one retain deterministic model-default lookup)
            const auto defaults =
                ASSETS::MODELS::ResolveModelMaterialBindings(
                    assetRegistry,
                    AssetGuid{modelComponent.GetAssetId()},
                    modelComponent.GetMaterialSetAssetGuid(),
                    modelAsset->materials,
                    !modelComponent.HasAuthoredMaterialSetSelection());
            for (const std::string& diagnostic : defaults.diagnostics) {
                HIKARI_LOG_WARN("[MaterialRuntime][ModelDefault] " + diagnostic);
            }
            for (const auto& binding : defaults.bindings) {
                if (binding.materialDescriptor != nullptr) {
                    (void)applyMaterial(
                        binding.materialIndex,
                        *binding.materialDescriptor,
                        true);
                }
            }

            // model default binding -> scene instance override
            for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                if (!slot.materialAssetGuid.IsValid()) {
                    continue;
                }

                const auto* descriptor = assetRegistry.FindAs<MaterialAssetDescriptor>(
                    AssetId{ slot.materialAssetGuid.value });
                if (!descriptor) {
                    continue;
                }

                const ASSETS::MODELS::MaterialSlotResolution resolution =
                    ASSETS::MODELS::ResolveMaterialSlot(
                        modelAsset->materials,
                        slot.slotId,
                        slot.slotIndex);
                if (!resolution.IsValid()) {
                    HIKARI_LOG_WARN(
                        "[MaterialRuntime] model material slot is no longer present; "
                        "keeping imported material");
                    continue;
                }
                if (resolution.source ==
                    ASSETS::MODELS::MaterialSlotResolutionSource::LegacyIndex &&
                    slot.slotId.IsValid()) {

                    HIKARI_LOG_WARN(
                        "[MaterialRuntime] stable material slot was not found; "
                        "using legacy slot index fallback");
                }

                (void)applyMaterial(
                    resolution.materialIndex, *descriptor, false);
            }
        }

        void ApplySandSurfaceMaterialOverrides(
            World& world,
            const AssetRegistry& assetRegistry) {

            world.ForEachObjectWith<
                ModelComponent,
                TerrainSurfaceComponent>(
                WorldComponentQueryAnchor::SmallestUniqueBucket,
                [&](GameObject&,
                    ModelComponent& modelComponent,
                    TerrainSurfaceComponent&) {
                    (void)SAND::EnsureSandSurfaceMaterialOverride(
                        modelComponent,
                        assetRegistry);
                });
        }

    }

    std::unique_ptr<Material> BuildRuntimeMaterialOverride(
        const AssetRegistry& assetRegistry,
        const MaterialAssetDescriptor& descriptor,
        bool inheritModelTextures) {

        auto runtimeMaterial = std::make_unique<Material>();
        MaterialRuntimeBuilder builder{};
        auto& programRegistry =
            RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
        if (!builder.BuildRuntimeMaterial(
                descriptor.sourceData,
                descriptor.compileResult,
                assetRegistry,
                programRegistry,
                *runtimeMaterial,
                descriptor.id.value)) {
            return nullptr;
        }
        runtimeMaterial->SetInheritModelTextures(inheritModelTextures);
        return runtimeMaterial;
    }

    ModelAsset* ResolveModelAssetNow(
        ModelManager& modelManager,
        const AssetRegistry& assetRegistry,
        const std::string& assetId) {

        return ResolveModelAssetNowInternal(
            modelManager,
            assetRegistry,
            assetId);
    }

    SceneDependencySet SceneRuntimeBuilder::CollectDependencies(
        const SceneDocument& document,
        const ComponentRegistry* enabledComponents) const {
        SceneDependencySet deps{};

        if (!document.environment.sky.skyAsset.empty()) {
            deps.skyAssetIds.insert(document.environment.sky.skyAsset);
        }
        if (document.environment.sky.mode == SkyMode::Texture2D &&
            !document.environment.sky.panoramaCloudAsset.empty()) {
            deps.skyAssetIds.insert(
                document.environment.sky.panoramaCloudAsset);
        }
        deps.lightingSceneHash =
            ASSETS::LIGHTING::BuildLightingSceneHash(document);
        deps.lightingBakeAuthoringHashes =
            ASSETS::LIGHTING::
                BuildLightingBakeRecordAuthoringHashes(document);
        deps.lighting = LIGHTING_AUTHORING::BuildSceneLightingSet(document);

        for (const SceneObjectData& object : document.objects) {
            for (const SceneComponentData& component : object.components) {
                if (enabledComponents != nullptr &&
                    enabledComponents->Find(component.type) == nullptr) {
                    continue;
                }
                if (component.type == "ModelComponent") {
                    const std::string assetId = component.properties.value("assetId", std::string{});
                    if (!assetId.empty()) {
                        deps.modelAssetIds.insert(assetId);
                    }
                    const std::string materialSetGuid =
                        component.properties.value(
                            "materialSetAssetGuid",
                            std::string{});
                    if (!materialSetGuid.empty()) {
                        deps.modelMaterialSetAssetIds.insert(
                            materialSetGuid);
                    }
                    if (component.properties.contains("materialOverrides") &&
                        component.properties["materialOverrides"].is_array()) {
                        for (const nlohmann::json& node : component.properties["materialOverrides"]) {
                            if (!node.is_object()) {
                                continue;
                            }
                            const std::string materialGuid = node.value("materialAssetGuid", std::string{});
                            if (!materialGuid.empty()) {
                                deps.materialAssetIds.insert(materialGuid);
                            }
                        }
                    }
                } else if (component.type == "VfxPlayerComponent") {
                    const auto slots =
                        component.properties.find("slots");
                    if (slots == component.properties.end() ||
                        !slots->is_array()) {
                        continue;
                    }
                    for (const nlohmann::json& slot : *slots) {
                        if (!slot.is_object()) {
                            continue;
                        }
                        const std::string assetGuid = slot.value(
                            "effectAssetGuid", std::string{});
                        if (!assetGuid.empty()) {
                            deps.vfxAssetIds.insert(assetGuid);
                        }
                    }
                }
            }
        }

        return deps;
    }

    bool SceneRuntimeBuilder::PreloadDependencies(
        const SceneDependencySet& dependencies,
        const AssetRegistry& assetRegistry,
        ModelManager& modelManager,
        SkyManager& skyManager,
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid) const {
        RuntimeAssetLoadPlan loadPlan{};

        auto addModelToLoadPlan = [&](
            const std::string& modelId,
            const ModelAssetDescriptor& descriptor) {

            loadPlan.modelAssetIds.push_back(modelId);
            EnsureModelLoaded(modelManager, descriptor);
            if (const ModelAsset* asset = modelManager.FindAsset(modelId);
                asset != nullptr && asset->GetState() == ModelAsset::State::Loaded) {
                CollectModelTextureRequests(loadPlan, *asset);
            }
            AddClusterGeometryRequest(loadPlan, projectRoot, descriptor);
        };

        for (const std::string& modelId : dependencies.modelAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ modelId });
            if (!descriptor) {
                continue;
            }
            addModelToLoadPlan(modelId, *descriptor);
            const ModelAsset* modelAsset = modelManager.FindAsset(modelId);
            if (modelAsset == nullptr) {
                continue;
            }
            const auto defaults =
                ASSETS::MODELS::ResolveDefaultModelMaterialBindings(
                    assetRegistry,
                    AssetGuid{modelId},
                    modelAsset->materials);
            for (const auto& binding : defaults.bindings) {
                if (binding.materialDescriptor != nullptr) {
                    CollectMaterialTextureRequests(
                        loadPlan,
                        assetRegistry,
                        *binding.materialDescriptor);
                }
            }
        }

        for (const std::string& materialSetId :
            dependencies.modelMaterialSetAssetIds) {
            const auto* materialSet =
                assetRegistry.FindAs<ModelMaterialBindingAssetDescriptor>(
                    AssetId{materialSetId});
            if (materialSet == nullptr) {
                continue;
            }
            for (const auto& binding : materialSet->data.bindings) {
                const auto* material =
                    assetRegistry.FindAs<MaterialAssetDescriptor>(
                        AssetId{binding.materialAssetGuid.value});
                if (material != nullptr) {
                    CollectMaterialTextureRequests(
                        loadPlan,
                        assetRegistry,
                        *material);
                }
            }
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<SkyAssetDescriptor>(AssetId{ skyId });
            if (!descriptor) {
                if (const auto* textureDescriptor =
                        assetRegistry.FindAs<TextureAssetDescriptor>(
                            AssetId{ skyId });
                    textureDescriptor != nullptr &&
                    textureDescriptor->dimension ==
                        TextureAssetDimension::Texture2D) {
                    const RENDER3D::TextureResourceColorSpace colorSpace =
                        textureDescriptor->colorSpace ==
                            TextureAssetColorSpace::Linear
                        ? RENDER3D::TextureResourceColorSpace::Linear
                        : RENDER3D::TextureResourceColorSpace::Srgb;
                    AddTextureRequest(
                        loadPlan,
                        "runtime_sky_panorama/" + skyId,
                        textureDescriptor->sourcePath,
                        colorSpace);
                }
                continue;
            }

            if (!descriptor->meshAssetId.empty()) {
                if (const auto* meshDescriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{ descriptor->meshAssetId })) {
                    addModelToLoadPlan(descriptor->meshAssetId, *meshDescriptor);
                }
            }
        }

        for (const std::string& materialId : dependencies.materialAssetIds) {
            const auto* descriptor = assetRegistry.FindAs<MaterialAssetDescriptor>(AssetId{ materialId });
            if (descriptor == nullptr) {
                continue;
            }
            CollectMaterialTextureRequests(loadPlan, assetRegistry, *descriptor);
        }

        for (const std::string& vfxId : dependencies.vfxAssetIds) {
            const auto* vfx = assetRegistry.FindAs<VfxAssetDescriptor>(
                AssetId{ vfxId });
            if (vfx == nullptr || !vfx->program) {
                continue;
            }
            for (const VFX::VfxProgramEmitter& emitter :
                 vfx->program->emitters) {
                if (emitter.render.materialMode !=
                        RENDER3D::PARTICLES::
                            ParticleMaterialMode::StandardUnlit ||
                    emitter.render.colorTextureAssetGuid.empty()) {
                    continue;
                }
                const auto* texture =
                    assetRegistry.FindAs<TextureAssetDescriptor>(
                        AssetId{
                            emitter.render.colorTextureAssetGuid });
                if (texture != nullptr &&
                    !texture->sourcePath.empty()) {
                    AddTextureRequest(
                        loadPlan,
                        "runtime_vfx/" + vfxId + "/" +
                            emitter.render.colorTextureAssetGuid,
                        texture->sourcePath,
                        RENDER3D::TextureResourceColorSpace::Srgb);
                }
            }
            for (const AssetGuid& materialGuid :
                    vfx->program->dependencyGuids) {
                const auto* material =
                    assetRegistry.FindAs<MaterialAssetDescriptor>(
                        AssetId{ materialGuid.value });
                if (material != nullptr) {
                    CollectMaterialTextureRequests(
                        loadPlan,
                        assetRegistry,
                        *material);
                    continue;
                }
                const auto* texture =
                    assetRegistry.FindAs<TextureAssetDescriptor>(
                        AssetId{ materialGuid.value });
                if (texture != nullptr &&
                    !texture->sourcePath.empty()) {
                    AddTextureRequest(
                        loadPlan,
                        "runtime_vfx/" + vfxId + "/" +
                            materialGuid.value,
                        texture->sourcePath,
                        RENDER3D::TextureResourceColorSpace::Srgb);
                }
            }
        }

        HIKARI_LOG_INFO(
            "[SceneRuntimeBuilder][LoadPlan] models=" + std::to_string(loadPlan.modelAssetIds.size()) +
            " vfx=" + std::to_string(dependencies.vfxAssetIds.size()) +
            " textures=" + std::to_string(loadPlan.textureRequests.size()) +
            " clusterGeometry=" + std::to_string(loadPlan.clusterGeometryRequests.size()));
        ExecuteRuntimeAssetLoadPlan(loadPlan);

        RENDER3D::LIGHTING::LightingRuntimeLoadRequest request{};
        request.sceneGuid = sceneGuid;
        request.projectRoot = projectRoot;
        request.skyAssetIds = dependencies.skyAssetIds;
        request.expectedSceneHash =
            dependencies.lightingSceneHash;
        request.expectedBakeAuthoringHashes =
            dependencies.lightingBakeAuthoringHashes;
        request.lighting = dependencies.lighting;

        // 照明リソースは runtime loader に委譲する。
        RENDER3D::LIGHTING::LightingRuntimeLoader loader{};
        const RENDER3D::LIGHTING::LightingRuntimeLoadResult lightingResult =
            loader.Load(request, assetRegistry, skyManager);
        for (const std::string& message : lightingResult.messages) {
            HIKARI_LOG_INFO(message);
        }
        return lightingResult.success;
    }

    bool SceneRuntimeBuilder::BuildWorldFromDocument(
        const SceneDocument& document,
        World& world,
        const AssetRegistry& assetRegistry,
        const ComponentRegistry& componentRegistry,
        ModelManager& modelManager,
        SkyManager&) const {
        world.Clear();

        constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

        for (const SceneObjectData& objectData : document.objects) {
            GameObject* object = world.CreateObject(objectData.name);
            if (!object->SetDocumentId(objectData.id)) {
                HIKARI_LOG_ERROR(
                    "[SceneRuntime] duplicate object id rejected: " +
                    std::to_string(objectData.id.value));
                world.Clear();
                return false;
            }
            Transform3D localTransform{};
            localTransform.position = objectData.transform.position;
            localTransform.scale = objectData.transform.scale;
            localTransform.rotation = MATH::Quat::FromEulerXYZ(
                objectData.transform.rotationEulerDeg.x * kDegToRad,
                objectData.transform.rotationEulerDeg.y * kDegToRad,
                objectData.transform.rotationEulerDeg.z * kDegToRad);
            (void)object->SetLocalTransform(localTransform);

            for (const SceneComponentData& componentData : objectData.components) {
                IComponent* component = componentRegistry.AddComponentToObject(*object, componentData.type);
                if (!component) {
                    HIKARI_LOG_WARN(
                        "[SceneRuntime] unavailable component skipped: " +
                        componentData.type +
                        " objectId=" + std::to_string(objectData.id.value));
                    continue;
                }
                std::string deserializeDiagnostic{};
                if (!component->TryDeserialize(
                        componentData.properties,
                        deserializeDiagnostic)) {
                    HIKARI_LOG_ERROR(
                        "[SceneRuntime] component deserialize failed: " +
                        componentData.type +
                        " objectId=" +
                        std::to_string(objectData.id.value) +
                        " diagnostic=" + deserializeDiagnostic);
                    world.Clear();
                    return false;
                }

                if (auto* modelComponent = dynamic_cast<ModelComponent*>(component)) {
                    modelComponent->SetModelAsset(ResolveModelAssetNow(
                        modelManager,
                        assetRegistry,
                        modelComponent->GetAssetId()));
                }
            }

            // 全コンポーネントを反映した後に、プログラム化モデルを含む
            // レンダリング可能アセットへ解決してマテリアルオーバーライドを構築する。
            // (ProceduralMeshComponent が後続の順序で追加されるケースを吸収)
            for (const std::unique_ptr<IComponent>& component :
                object->GetComponents()) {
                if (auto* modelComponent =
                        dynamic_cast<ModelComponent*>(component.get())) {
                    const std::string* proceduralSelection = nullptr;
                    if (const auto* procedural =
                            object->GetComponent<
                                ProceduralMeshComponent>()) {
                        proceduralSelection =
                            &procedural->GetMaterialSelection();
                    }
                    BuildModelMaterialOverrides(
                        *modelComponent,
                        assetRegistry,
                        ResolveRenderableModelAsset(
                            *object, *modelComponent),
                        proceduralSelection);
                }
            }
        }

        ApplySandSurfaceMaterialOverrides(world, assetRegistry);

        for (const SceneObjectData& objectData : document.objects) {
            if (!objectData.parent.has_value()) {
                continue;
            }
            GameObject* child = world.FindObject(objectData.id);
            GameObject* parent = world.FindObject(*objectData.parent);
            if (child == nullptr || parent == nullptr ||
                !child->SetParent(parent)) {
                HIKARI_LOG_WARN(
                    "[SceneRuntime] invalid object hierarchy skipped: child=" +
                    std::to_string(objectData.id.value) +
                    " parent=" +
                    std::to_string(objectData.parent->value));
            }
        }

        return true;
    }

} // namespace HIKARI
