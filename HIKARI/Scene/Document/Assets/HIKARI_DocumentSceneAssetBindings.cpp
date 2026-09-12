#include "Core/HIKARI_StartupTimer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Assets/HIKARI_DocumentSceneAssetBindings.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Render2D/HIKARI_DxTexture.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingResolver.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        bool ImportOutdatedShaderArtifacts(AssetDatabase& database) {
            if (database.IsCookedContent()) {
                return true;
            }

            std::vector<AssetGuid> shaderGuids{};
            for (const AssetRecord* record :
                database.CollectByType(AssetType::Shader)) {
                if (record != nullptr && record->importOutdated) {
                    shaderGuids.push_back(record->guid);
                }
            }
            if (shaderGuids.empty()) {
                return true;
            }

            const AssetImportBatchResult imported =
                database.ImportAssets(shaderGuids);
            if (imported.failed != 0 ||
                imported.succeeded !=
                    static_cast<int>(shaderGuids.size())) {
                HIKARI_LOG_ERROR(
                    "[ShaderImporter] authoring startup cook failed. "
                    "attempted=" + std::to_string(imported.attempted) +
                    " succeeded=" + std::to_string(imported.succeeded) +
                    " failed=" + std::to_string(imported.failed));
                return false;
            }

            HIKARI_LOG_INFO(
                "[ShaderImporter] authoring startup cooked " +
                std::to_string(imported.succeeded) +
                " outdated shader asset(s).");
            return true;
        }
#endif

        ASSETS::MODELS::MaterialSlotResolution ResolveOverrideSlot(
            const ModelAsset* model,
            const ModelMaterialOverrideSlot& slot) {

            if (model == nullptr) {
                return {};
            }
            return ASSETS::MODELS::ResolveMaterialSlot(
                model->materials,
                slot.slotId,
                slot.slotIndex);
        }

        bool HasMissingAssetDescriptors(
            const AssetRegistry& registry,
            AssetType expectedType,
            const std::unordered_set<std::string>& assetIds) {

            for (const std::string& assetId : assetIds) {
                if (assetId.empty()) {
                    continue;
                }

                const AssetDescriptor* descriptor = registry.FindDescriptor(assetId);
                if (descriptor == nullptr || descriptor->type != expectedType) {
                    return true;
                }
            }

            return false;
        }

        bool HasMissingSkyResourceDescriptors(
            const AssetRegistry& registry,
            const std::unordered_set<std::string>& assetIds) {

            for (const std::string& assetId : assetIds) {
                if (assetId.empty()) {
                    continue;
                }
                if (assetId ==
                    RENDER3D::LIGHTING::kDefaultSkyAssetName) {
                    continue;
                }
                const AssetDescriptor* descriptor =
                    registry.FindDescriptor(assetId);
                if (descriptor == nullptr ||
                    (descriptor->type != AssetType::Sky &&
                     descriptor->type != AssetType::Texture)) {
                    return true;
                }
            }
            return false;
        }

        bool IsCookedTextureRuntimePath(const std::filesystem::path& path) {
            if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) ==
                CookedAssetFormat::HTEX) {
                return true;
            }

            const std::string generic = TEXT::ToLowerAsciiCopy(path.generic_string());
            return generic.find("library/imported/") != std::string::npos;
        }
    }

    bool DocumentSceneAssetBindings::ReloadAssets() {
        CORE::StartupTimer assetsTimer("Scene.reload_assets");
        if (database_.GetProjectRoot().empty()) {
            if (!database_.Initialize({})) {
                return false;
            }
        }
        AssetDatabase::ContentSnapshot previousDatabase =
            {};
        if (!database_.TryTakeContentSnapshot(previousDatabase)) {
            return false;
        }
        const auto restoreDatabase = [&]() noexcept {
            database_.RestoreContentSnapshot(
                std::move(previousDatabase));
        };
        AssetRegistry nextRegistry{};
        try {
            if (!database_.ScanAssets(true)) {
                restoreDatabase();
                return false;
            }
#if defined(HIKARI_WITH_EDITOR)
            if (!ImportOutdatedShaderArtifacts(database_)) {
                restoreDatabase();
                return false;
            }
#endif

            // Database and Registry publish together. A corrupt asset must
            // not replace the last complete live snapshot. Successfully
            // written metadata and cooked artifacts remain reusable inputs;
            // rollback applies to live publication only.
            AssetRegistryBuilder assetRegistryBuilder{};
            if (!assetRegistryBuilder.RebuildRegistry(
                    database_, nextRegistry)) {
                restoreDatabase();
                return false;
            }
            registry_.SwapContents(nextRegistry);
            registrySourceRevision_ = database_.GetContentRevision();
        }
        catch (const std::exception& error) {
            HIKARI_LOG_ERROR(
                std::string("[SceneAsset] registry publication threw: ") +
                error.what());
            restoreDatabase();
            return false;
        }
        catch (...) {
            HIKARI_LOG_ERROR(
                "[SceneAsset] registry publication threw an unknown "
                "exception.");
            restoreDatabase();
            return false;
        }

        ConfigureModelTextureResolver();
        RenderSubmissionSystem::InvalidateSceneResources(false);
        return true;
    }
    void DocumentSceneAssetBindings::ConfigureModelTextureResolver() {
        models_.ResetTextureResolveStats();
        models_.SetTexturePathResolver(
            [this](const std::string& sourceTexturePath, MaterialTextureUsage usage) -> std::string {
                return ResolveModelTexturePath(sourceTexturePath, usage);
            });
    }

    const AssetRecord* DocumentSceneAssetBindings::FindUniqueTextureAssetByFilename(
        const std::string& filename,
        const std::string& sourceTexturePath) const {

        if (filename.empty()) {
            return nullptr;
        }

        const std::string target = TEXT::ToLowerAsciiCopy(filename);
        const AssetRecord* matchedRecord = nullptr;
        int matchCount = 0;

        for (const AssetRecord* record : database_.CollectByType(AssetType::Texture)) {
            if (!record) {
                continue;
            }

            const std::string recordFilename = TEXT::ToLowerAsciiCopy(record->sourcePath.filename().string());
            if (recordFilename != target) {
                continue;
            }

            matchedRecord = record;
            ++matchCount;
        }

        if (matchCount > 1) {
            models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Ambiguous);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " reason=ambiguous filename matches filename=" + filename +
                " count=" + std::to_string(matchCount));
            return nullptr;
        }

        return matchCount == 1 ? matchedRecord : nullptr;
    }

    std::string DocumentSceneAssetBindings::ResolveModelTexturePath(
        const std::string& sourceTexturePath,
        MaterialTextureUsage usage) const {

        if (sourceTexturePath.empty()) {
            return {};
        }

        std::filesystem::path sourcePath = std::filesystem::path(sourceTexturePath).lexically_normal();
        if (IsCookedTextureRuntimePath(sourcePath)) {
            return sourcePath.generic_string();
        }

        const AssetRecord* record = database_.FindByPath(sourcePath);

        if (!record && !database_.GetProjectRoot().empty()) {
            std::error_code ec{};
            const std::filesystem::path absolutePath = sourcePath.is_absolute()
                ? sourcePath.lexically_normal()
                : (database_.GetProjectRoot() / sourcePath).lexically_normal();
            const std::filesystem::path relativePath =
                std::filesystem::relative(absolutePath, database_.GetProjectRoot(), ec).lexically_normal();
            if (!ec && !relativePath.empty()) {
                record = database_.FindByPath(relativePath);
            }
            if (!record) {
                record = database_.FindByPath(absolutePath);
            }
        }

        if (!record) {
            record = FindUniqueTextureAssetByFilename(sourcePath.filename().string(), sourceTexturePath);
        }

        if (!record) {
            models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " reason=texture asset not found");
            return sourceTexturePath;
        }

        if (record->type != AssetType::Texture || !record->guid.IsValid()) {
            models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " reason=resolved asset is not a texture");
            return sourceTexturePath;
        }

        const auto* descriptor = registry_.FindAs<TextureAssetDescriptor>(AssetId{ record->guid.value });
        if (!descriptor || descriptor->sourcePath.empty()) {
            models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
            HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" +
                sourceTexturePath +
                " usage=" + MaterialTextureUsageName(usage) +
                " reason=texture descriptor missing");
            return sourceTexturePath;
        }

        return descriptor->sourcePath;
    }

    bool DocumentSceneAssetBindings::NeedsRuntimeDependencyRegistryRefresh(
        const SceneDependencySet& dependencies) const {
        const bool hasDescriptorDependencies =
            !dependencies.modelAssetIds.empty() ||
            !dependencies.skyAssetIds.empty() ||
            !dependencies.textureAssetIds.empty() ||
            !dependencies.materialAssetIds.empty() ||
            !dependencies.modelMaterialSetAssetIds.empty() ||
            !dependencies.vfxAssetIds.empty();
        return
            (hasDescriptorDependencies &&
             registrySourceRevision_ != database_.GetContentRevision()) ||
            HasMissingAssetDescriptors(
                registry_,
                AssetType::Model,
                dependencies.modelAssetIds) ||
            HasMissingAssetDescriptors(
                registry_,
                AssetType::Material,
                dependencies.materialAssetIds) ||
            HasMissingAssetDescriptors(
                registry_,
                AssetType::ModelMaterialBinding,
                dependencies.modelMaterialSetAssetIds) ||
            HasMissingAssetDescriptors(
                registry_,
                AssetType::VfxEffect,
                dependencies.vfxAssetIds) ||
            HasMissingAssetDescriptors(
                registry_,
                AssetType::Texture,
                dependencies.textureAssetIds) ||
            HasMissingSkyResourceDescriptors(
                registry_,
                dependencies.skyAssetIds);
    }

    bool DocumentSceneAssetBindings::RefreshTextureByPath(const std::string& path) {
        if (path.empty()) {
            return false;
        }

        DXTEX::DxTextureManager::InvalidateTextureCacheByPath(path);
        return true;
    }

    bool DocumentSceneAssetBindings::ReloadModelAsset(const AssetId& modelId) {
        if (modelId.value.empty()) {
            return false;
        }

        const bool reloaded = models_.ReloadAssetNow(modelId.value);
        if (reloaded) {
            RenderSubmissionSystem::InvalidateSceneResources(false);
        }
        return reloaded;
    }

    int DocumentSceneAssetBindings::RebindModelComponents(DocumentSceneBase& scene) {
        int reboundCount = 0;
        SceneDependencySet deps = scene.state_->runtime.builder.CollectDependencies(
            scene.state_->identity.document,
            &scene.state_->runtime.componentRegistry);
        if (NeedsRuntimeDependencyRegistryRefresh(deps)) {
            if (!scene.ReloadAssets()) {
                HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before model rebind.");
                return 0;
            }
            deps = scene.state_->runtime.builder.CollectDependencies(
                scene.state_->identity.document,
                &scene.state_->runtime.componentRegistry);
            if (NeedsRuntimeDependencyRegistryRefresh(deps)) {
                HIKARI_LOG_WARN(
                    "[SceneRuntime] model rebind rejected because required "
                    "asset descriptors remain unavailable.");
                return 0;
            }
        }

        scene.state_->runtime.builder.PreloadDependencies(
            deps,
            registry_,
            models_,
            scene.state_->lighting.sky,
            database_.GetProjectRoot(),
            scene.state_->identity.currentSceneAssetGuid.value);
        SKYRENDERER::InvalidateSkyTextureCache();
        RENDER3D::PANORAMA_SKY::
            InvalidatePanoramaSkyLayerTextureCache();

        scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
            [this, &reboundCount](GameObject&, ModelComponent& modelComponent) {
                modelComponent.SetModelAsset(models_.FindAsset(modelComponent.GetAssetId()));
                ++reboundCount;
            });

        RebuildMaterialOverrides(scene);
        if (reboundCount > 0) {
            RenderSubmissionSystem::InvalidateSceneResources(false);
        }
        return reboundCount;
    }

    int DocumentSceneAssetBindings::RebuildMaterialOverrides(DocumentSceneBase& scene) {
        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};

        // Material override 邵ｺ・ｯ scene load / refresh 隴弱ｅ笆｡邵ｺ螟ｧ繝ｻ隶堤距・ｯ蟲ｨ笘・ｹｧ荵敖繝ｻ
        scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder](
                GameObject& object, ModelComponent& modelComponent) {
                modelComponent.ClearRuntimeMaterialOverrides();

                auto& programRegistry =
                    RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
                // 程序化モデルは ModelComponent に保持されないため、
                // レンダリングと同じ「プログラム化対応」の解決を使う。
                const ModelAsset* modelAsset =
                    ResolveRenderableModelAsset(object, modelComponent);
                // プロシージャル形状は ProceduralMeshComponent が
                // マテリアル選択を所有する。
                if (const auto* procedural =
                        object.GetComponent<
                            ProceduralMeshComponent>()) {
                    const std::string& selection =
                        procedural->GetMaterialSelection();
                    if (!selection.empty()) {
                        const auto* descriptor =
                            registry_.FindAs<MaterialAssetDescriptor>(
                                AssetId{ selection });
                        if (descriptor != nullptr) {
                            auto runtimeMaterial =
                                std::make_unique<Material>();
                            if (materialBuilder.BuildRuntimeMaterial(
                                    descriptor->sourceData,
                                    descriptor->compileResult,
                                    registry_,
                                    programRegistry,
                                    *runtimeMaterial,
                                    descriptor->id.value)) {
                                modelComponent.SetRuntimeMaterialOverride(
                                    0, std::move(runtimeMaterial));
                                ++rebuiltCount;
                            }
                        } else {
                            HIKARI_LOG_WARN(
                                "[MaterialRuntime][Procedural] material "
                                "selection not registered: " +
                                selection);
                        }
                    }
                    return;
                }
                if (modelAsset != nullptr) {
                    const auto defaults =
                        ASSETS::MODELS::ResolveModelMaterialBindings(
                            registry_,
                            AssetGuid{modelComponent.GetAssetId()},
                            modelComponent.GetMaterialSetAssetGuid(),
                            modelAsset->materials,
                            !modelComponent.HasAuthoredMaterialSetSelection());
                    for (const std::string& diagnostic : defaults.diagnostics) {
                        HIKARI_LOG_WARN(
                            "[MaterialRuntime][ModelDefault] " + diagnostic);
                    }
                    for (const auto& binding : defaults.bindings) {
                        if (binding.materialDescriptor == nullptr) {
                            continue;
                        }
                        auto runtimeMaterial = std::make_unique<Material>();
                        if (materialBuilder.BuildRuntimeMaterial(
                                binding.materialDescriptor->sourceData,
                                binding.materialDescriptor->compileResult,
                                registry_,
                                programRegistry,
                                *runtimeMaterial,
                                binding.materialDescriptor->id.value)) {
                            runtimeMaterial->SetInheritModelTextures(true);
                            modelComponent.SetRuntimeMaterialOverride(
                                binding.materialIndex,
                                std::move(runtimeMaterial));
                            ++rebuiltCount;
                        }
                    }
                }

                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (!slot.materialAssetGuid.IsValid()) {
                        continue;
                    }

                    const auto resolution = ResolveOverrideSlot(
                        modelAsset,
                        slot);
                    if (!resolution.IsValid()) {
                        continue;
                    }

                    const auto* descriptor = registry_.FindAs<MaterialAssetDescriptor>(
                        AssetId{ slot.materialAssetGuid.value });
                    if (!descriptor) {
                        HIKARI_LOG_WARN("[MaterialRuntime] material asset not registered: " +
                            slot.materialAssetGuid.value);
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            descriptor->sourceData,
                            descriptor->compileResult,
                            registry_,
                            programRegistry,
                            *runtimeMaterial,
                            descriptor->id.value)) {
                        modelComponent.SetRuntimeMaterialOverride(
                            resolution.materialIndex,
                            std::move(runtimeMaterial));
                        ++rebuiltCount;
                    }
                }
            });

        return rebuiltCount;
    }

    int DocumentSceneAssetBindings::RebuildMaterialOverridesForMaterial(
        DocumentSceneBase& scene,
        const AssetGuid& materialGuid) {
        if (!materialGuid.IsValid()) {
            return 0;
        }

        // A material can be consumed by a model-level default binding as well
        // as a scene instance override. Rebuild the canonical chain so both
        // consumers update together and unavailable assets fall back per slot.
        return RebuildMaterialOverrides(scene);
    }

    int DocumentSceneAssetBindings::ApplyMaterialOverridePreview(
        DocumentSceneBase& scene,
        const AssetGuid& materialGuid,
        const MaterialAssetData& data) {

        if (!materialGuid.IsValid()) {
            return 0;
        }

        MaterialAssetData effectiveData = data;
        MaterialCompileResult effectiveCompile = CompileMaterialAsset(
            effectiveData,
            ASSETS::MATERIAL::MakeMaterialCompileOptions(database_));
        if (effectiveData.kind == MaterialAssetKind::Instance) {
            ASSETS::MATERIAL::MaterialAssetAuthoringService service(database_);
            const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved =
                service.ResolveInstance(effectiveData);
            if (!resolved.success) {
                HIKARI_LOG_WARN(
                    "[MaterialRuntime][HotPreview] instance composition failed: " +
                    materialGuid.value);
                return 0;
            }
            effectiveData = resolved.resolvedMaterial;
            effectiveCompile = resolved.compile;
        }

        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};
        auto& programRegistry =
            RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
        scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder, &programRegistry,
             &materialGuid, &effectiveData,
             &effectiveCompile](GameObject& object,
                                ModelComponent& modelComponent) {
                const auto applyPreview = [this, &rebuiltCount,
                    &materialBuilder, &programRegistry, &materialGuid,
                    &effectiveData, &effectiveCompile,
                    &modelComponent](uint32_t materialIndex,
                                     bool inheritModelTextures) {
                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            effectiveData,
                            effectiveCompile,
                            registry_,
                            programRegistry,
                            *runtimeMaterial,
                            materialGuid.value)) {
                        runtimeMaterial->SetInheritModelTextures(
                            inheritModelTextures);
                        modelComponent.SetRuntimeMaterialOverride(
                            materialIndex,
                            std::move(runtimeMaterial));
                        ++rebuiltCount;
                    } else {
                        modelComponent.ClearRuntimeMaterialOverride(
                            materialIndex);
                    }
                };

                struct EffectiveMaterialBinding {
                    AssetGuid guid{};
                    bool inheritModelTextures = false;
                };
                std::unordered_map<uint32_t, EffectiveMaterialBinding>
                    effectiveMaterialByIndex{};
                const ModelAsset* modelAsset =
                    ResolveRenderableModelAsset(
                        object, modelComponent);
                if (modelAsset != nullptr) {
                    const auto materialSet =
                        ASSETS::MODELS::ResolveModelMaterialBindings(
                            registry_,
                            AssetGuid{modelComponent.GetAssetId()},
                            modelComponent.GetMaterialSetAssetGuid(),
                            modelAsset->materials,
                            !modelComponent.HasAuthoredMaterialSetSelection());
                    for (const auto& binding : materialSet.bindings) {
                        if (binding.materialAssetGuid.IsValid()) {
                            effectiveMaterialByIndex[binding.materialIndex] =
                                EffectiveMaterialBinding{
                                    binding.materialAssetGuid, true};
                        }
                    }
                }

                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.materialAssetGuid != materialGuid) {
                        continue;
                    }

                    const auto resolution = ResolveOverrideSlot(
                        modelAsset,
                        slot);
                    if (!resolution.IsValid()) {
                        continue;
                    }

                    effectiveMaterialByIndex[resolution.materialIndex] =
                        EffectiveMaterialBinding{
                            slot.materialAssetGuid, false};
                }

                for (const auto& [materialIndex, effectiveBinding] :
                        effectiveMaterialByIndex) {
                    if (effectiveBinding.guid == materialGuid) {
                        applyPreview(
                            materialIndex,
                            effectiveBinding.inheritModelTextures);
                    }
                }
            });

        return rebuiltCount;
    }

    int DocumentSceneAssetBindings::ApplyMaterialOverridePreview(
        DocumentSceneBase& scene,
        const AssetGuid& materialGuid,
        const PbrMaterialAssetData& data) {

        return ApplyMaterialOverridePreview(
            scene,
            materialGuid,
            MakeStandardPbrMaterialAssetData(data));
    }

    bool DocumentSceneBase::ReloadAssets() {
        const bool reloaded = state_->assets.ReloadAssets();
        if (reloaded) {
            // AssetDatabase retains stable object identity across scans, so
            // pointer equality cannot invalidate cached .hasm definitions.
            // Publish the new registry first, then discard the old cache;
            // failed reloads keep the last-known-good definitions intact.
            state_->runtime.animationStateMachineAssets.Clear();
        }
        return reloaded;
    }

    bool DocumentSceneBase::RefreshTextureRuntimeByPath(
        const std::string& path) {
        return state_->assets.RefreshTextureByPath(path);
    }

    bool DocumentSceneBase::ReloadModelAssetRuntime(const AssetId& modelId) {
        return state_->assets.ReloadModelAsset(modelId);
    }

    int DocumentSceneBase::RebindModelComponents() {
        return state_->assets.RebindModelComponents(*this);
    }

    int DocumentSceneBase::RebuildMaterialOverrides() {
        return state_->assets.RebuildMaterialOverrides(*this);
    }

    int DocumentSceneBase::RebuildMaterialOverridesForMaterial(
        const AssetGuid& materialGuid) {
        return state_->assets.RebuildMaterialOverridesForMaterial(
            *this,
            materialGuid);
    }

    int DocumentSceneBase::ApplyRuntimeMaterialOverridePreview(
        const AssetGuid& materialGuid,
        const MaterialAssetData& data) {
        return state_->assets.ApplyMaterialOverridePreview(
            *this,
            materialGuid,
            data);
    }

    int DocumentSceneBase::ApplyRuntimeMaterialOverridePreview(
        const AssetGuid& materialGuid,
        const PbrMaterialAssetData& data) {
        return state_->assets.ApplyMaterialOverridePreview(
            *this,
            materialGuid,
            data);
    }


} // namespace HIKARI
