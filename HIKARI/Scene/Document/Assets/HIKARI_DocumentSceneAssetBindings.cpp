#include "Core/HIKARI_StartupTimer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Assets/HIKARI_DocumentSceneAssetBindings.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"

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
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"
#include "Assets/Prefab/HIKARI_PrefabAssetStore.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingResolver.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"

namespace HIKARI {
namespace {
#if defined(HIKARI_WITH_EDITOR)
std::vector<AssetGuid> CollectRequiredProgramImports(const AssetDatabase& database,
                                                     const SceneDependencySet& required) {
    std::vector<AssetGuid> guids;
    if (database.IsCookedContent())
        return guids;
    for (const auto& id : required.vfxAssetIds) {
        const auto* record = database.FindByGuid(AssetGuid{id});
        if (record && record->type == AssetType::VfxEffect &&
            (record->importOutdated || !record->lastImportSucceeded || record->artifactMissing))
            guids.push_back(record->guid);
    }
    return guids;
}

bool ImportRequiredAssetClosure(AssetDatabase& database, const AssetGuid& root, std::string& outError) {
    if (database.IsCookedContent()) {
        outError = "Cooked runtime assets cannot import source dependencies.";
        return false;
    }

    std::vector<AssetGuid> pending{root};
    std::unordered_set<std::string> visited{};
    for (size_t index = 0u; index < pending.size(); ++index) {
        const AssetGuid guid = pending[index];
        if (!guid.IsValid()) {
            outError = "Required asset manifest contains an invalid dependency GUID.";
            return false;
        }
        if (!visited.emplace(guid.value).second) {
            continue;
        }

        const AssetRecord* record = database.FindByGuid(guid);
        if (record == nullptr || record->type == AssetType::Unknown || record->duplicateGuid ||
            !record->guid.IsValid()) {
            outError = "Required asset is missing or cannot be imported: " + guid.value;
            return false;
        }
        const bool needsImport = record->importOutdated || !record->lastImportSucceeded || record->artifactMissing;
        if (needsImport && !record->CanImport()) {
            outError = "Required asset cannot be imported: " + guid.value;
            return false;
        }
        if (needsImport && !database.ImportAsset(guid)) {
            record = database.FindByGuid(guid);
            outError = "Required asset import failed: " + guid.value;
            if (record != nullptr && !record->lastImportMessage.empty()) {
                outError += " (" + record->lastImportMessage + ")";
            }
            return false;
        }

        record = database.FindByGuid(guid);
        if (record == nullptr || !record->lastImportSucceeded || record->importOutdated || record->artifactMissing) {
            outError = "Required asset did not produce a usable import: " + guid.value;
            if (record != nullptr && !record->lastImportMessage.empty()) {
                outError += " (" + record->lastImportMessage + ")";
            }
            return false;
        }
        for (const AssetDependencyDesc& dependency : record->artifactManifest.dependencies) {
            if (ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(dependency.role)) {
                continue;
            }
            pending.push_back(dependency.guid);
        }
    }
    return true;
}

bool ImportOutdatedShaderArtifacts(AssetDatabase& database) {
    if (database.IsCookedContent()) {
        return true;
    }

    std::vector<AssetGuid> shaderGuids{};
    for (const AssetRecord* record : database.CollectByType(AssetType::Shader)) {
        if (record != nullptr && record->importOutdated) {
            shaderGuids.push_back(record->guid);
        }
    }
    if (shaderGuids.empty()) {
        return true;
    }

    const AssetImportBatchResult imported = database.ImportAssets(shaderGuids);
    if (imported.failed != 0 || imported.succeeded != static_cast<int>(shaderGuids.size())) {
        HIKARI_LOG_ERROR("[ShaderImporter] authoring startup cook failed. "
                         "attempted=" +
                         std::to_string(imported.attempted) + " succeeded=" + std::to_string(imported.succeeded) +
                         " failed=" + std::to_string(imported.failed));
        return false;
    }

    HIKARI_LOG_INFO("[ShaderImporter] authoring startup cooked " + std::to_string(imported.succeeded) +
                    " outdated shader asset(s).");
    return true;
}

// 無頭の後台試玩（--agent-play-run、hikari headless）は編集器の画面や本地桥から取り込みを起こせないため、
// 場景を開く前に古い・未取り込みの資産を同期で取り込む。失敗した資産は記録だけして場景は開き、
// 通常の編集器起動（利用者が取り込みを操作する）の振る舞いは変えない。
void ImportOutdatedAssetsForHeadlessRun(AssetDatabase& database) {
    if (database.IsCookedContent() || !HasRuntimeCommandLineFlag(L"--agent-play-run"))
        return;
    const AssetImportBatchResult imported = database.ImportAllOutdated();
    if (imported.attempted == 0)
        return;
    const std::string summary = "[AssetImport] headless run imported outdated assets attempted=" +
                                std::to_string(imported.attempted) + " succeeded=" +
                                std::to_string(imported.succeeded) + " failed=" + std::to_string(imported.failed);
    if (imported.failed != 0)
        HIKARI_LOG_WARN(summary);
    else
        HIKARI_LOG_INFO(summary);
}
#endif

std::optional<uint32_t> ResolveOverrideSlot(const ModelAsset* model, const ModelMaterialOverrideSlot& slot) {
    return model != nullptr ? ASSETS::MODELS::FindMaterialSlotIndex(model->materials, slot.slotId) : std::nullopt;
}

bool HasMissingAssetDescriptors(const AssetRegistry& registry, AssetType expectedType,
                                const std::unordered_set<std::string>& assetIds, std::string* outReason) {
    for (const std::string& assetId : assetIds) {
        if (assetId.empty()) {
            continue;
        }

        const AssetDescriptor* descriptor = registry.FindDescriptor(assetId);
        if (descriptor == nullptr || descriptor->type != expectedType) {
            if (outReason)
                *outReason = "Unavailable asset '" + assetId + "' (expected " +
                             std::string(ASSETS::SEMANTICS::ToString(expectedType)) + ").";
            return true;
        }
    }

    return false;
}

bool HasMissingSkyResourceDescriptors(const AssetRegistry& registry, const std::unordered_set<std::string>& assetIds,
                                      std::string* outReason) {
    for (const std::string& assetId : assetIds) {
        if (assetId.empty()) {
            continue;
        }
        if (assetId == RENDER3D::LIGHTING::kDefaultSkyAssetName) {
            continue;
        }
        const AssetDescriptor* descriptor = registry.FindDescriptor(assetId);
        if (descriptor == nullptr || (descriptor->type != AssetType::Sky && descriptor->type != AssetType::Texture)) {
            if (outReason)
                *outReason = "Unavailable sky asset '" + assetId + "'.";
            return true;
        }
    }
    return false;
}

bool HasMissingSceneDependencyDescriptors(const AssetRegistry& registry, const SceneDependencySet& dependencies,
                                          std::string* outReason) {
    return HasMissingAssetDescriptors(registry, AssetType::Model, dependencies.modelAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::Material, dependencies.materialAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::ModelMaterialBinding, dependencies.modelMaterialSetAssetIds,
                                      outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::Sprite, dependencies.spriteAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::SpriteMaterial, dependencies.spriteMaterialAssetIds,
                                      outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::VfxEffect, dependencies.vfxAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry, AssetType::Texture, dependencies.textureAssetIds, outReason) ||
           HasMissingSkyResourceDescriptors(registry, dependencies.skyAssetIds, outReason);
}

#if defined(HIKARI_WITH_EDITOR)
bool IsImportedAssetClosureReady(const AssetDatabase& database, const AssetGuid& root) {
    std::vector<AssetGuid> pending{root};
    std::unordered_set<std::string> visited{};
    for (size_t index = 0u; index < pending.size(); ++index) {
        const AssetGuid guid = pending[index];
        if (!guid.IsValid()) {
            return false;
        }
        if (!visited.emplace(guid.value).second) {
            continue;
        }
        const AssetRecord* record = database.FindByGuid(guid);
        if (record == nullptr || record->duplicateGuid || !record->guid.IsValid() || !record->lastImportSucceeded ||
            record->importOutdated || record->artifactMissing) {
            return false;
        }
        for (const AssetDependencyDesc& dependency : record->artifactManifest.dependencies) {
            pending.push_back(dependency.guid);
        }
    }
    return true;
}
#endif

bool IsCookedTextureRuntimePath(const std::filesystem::path& path) {
    if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) == CookedAssetFormat::HTEX) {
        return true;
    }

    const std::string generic = TEXT::ToLowerAsciiCopy(path.generic_string());
    return generic.find("library/imported/") != std::string::npos;
}
} // namespace

bool DocumentSceneAssetBindings::ReloadAssets(
    const SceneDependencySet* required, const AssetGuid* admissionRoot,
    const std::function<bool(const AssetRegistry&, std::string&)>& validatePrepared, std::string* outError) {
#if !defined(HIKARI_WITH_EDITOR)
    (void)admissionRoot;
#endif
    if (outError != nullptr) {
        outError->clear();
    }
    CORE::StartupTimer assetsTimer("Scene.reload_assets");
    if (database_.GetProjectRoot().empty()) {
        if (!database_.Initialize({})) {
            if (outError != nullptr) {
                *outError = "Asset database initialization failed.";
            }
            return false;
        }
    }
    AssetDatabase::ContentSnapshot previousDatabase = {};
    if (!database_.TryTakeContentSnapshot(previousDatabase)) {
        if (outError != nullptr) {
            *outError = "Asset database snapshot is busy.";
        }
        return false;
    }
    const auto restoreDatabase = [&]() noexcept { database_.RestoreContentSnapshot(std::move(previousDatabase)); };
    AssetRegistry nextRegistry{};
    const uint64_t previousRegistrySourceRevision = registrySourceRevision_;
    bool registryPublished = false;
    try {
        CORE::StartupTimer scanTimer("Assets.scan");
        if (!database_.ScanAssets(true)) {
            if (outError != nullptr) {
                *outError = "Asset source scan failed.";
            }
            restoreDatabase();
            return false;
        }
        scanTimer.Finish();
#if defined(HIKARI_WITH_EDITOR)
        CORE::StartupTimer shaderTimer("Assets.refresh_shader_artifacts");
        if (!ImportOutdatedShaderArtifacts(database_)) {
            if (outError != nullptr) {
                *outError = "Shader artifact refresh failed.";
            }
            restoreDatabase();
            return false;
        }
        shaderTimer.Finish();
        ImportOutdatedAssetsForHeadlessRun(database_);
        if (admissionRoot != nullptr) {
            std::string importError{};
            if (!ImportRequiredAssetClosure(database_, *admissionRoot, importError)) {
                if (outError != nullptr) {
                    *outError = std::move(importError);
                }
                restoreDatabase();
                return false;
            }
        }
        if (required) {
            const auto programs = CollectRequiredProgramImports(database_, *required);
            if (!programs.empty()) {
                const auto imported = database_.ImportAssets(programs);
                if (imported.failed || imported.succeeded != static_cast<int>(programs.size())) {
                    HIKARI_LOG_ERROR(
                        "[SceneRuntime] required VFX program import failed; previous live assets preserved.");
                    if (outError != nullptr) {
                        *outError = "Required VFX program import failed.";
                    }
                    restoreDatabase();
                    return false;
                }
            }
        }
#endif

        // Database and Registry publish together. A corrupt asset must
        // not replace the last complete live snapshot. Successfully
        // written metadata and cooked artifacts remain reusable inputs;
        // rollback applies to live publication only.
        AssetRegistryBuilder assetRegistryBuilder{};
        CORE::StartupTimer registryTimer("Assets.build_registry");
        if (!assetRegistryBuilder.RebuildRegistry(database_, nextRegistry)) {
            if (outError != nullptr) {
                *outError = "Asset registry rebuild failed.";
            }
            restoreDatabase();
            return false;
        }
        if (required != nullptr) {
            std::string reason{};
            if (HasMissingSceneDependencyDescriptors(nextRegistry, *required, &reason)) {
                if (outError != nullptr) {
                    *outError = std::move(reason);
                }
                restoreDatabase();
                return false;
            }
        }
        registry_.SwapContents(nextRegistry);
        registrySourceRevision_ = database_.GetContentRevision();
        registryPublished = true;
        if (validatePrepared) {
            std::string reason{};
            if (!validatePrepared(registry_, reason)) {
                if (outError != nullptr) {
                    *outError = reason.empty() ? "Prepared runtime assets failed validation." : std::move(reason);
                }
                registry_.SwapContents(nextRegistry);
                registrySourceRevision_ = previousRegistrySourceRevision;
                registryPublished = false;
                restoreDatabase();
                return false;
            }
        }
    } catch (const std::exception& error) {
        HIKARI_LOG_ERROR(std::string("[SceneAsset] registry publication threw: ") + error.what());
        if (outError != nullptr) {
            *outError = error.what();
        }
        if (registryPublished) {
            registry_.SwapContents(nextRegistry);
            registrySourceRevision_ = previousRegistrySourceRevision;
        }
        restoreDatabase();
        return false;
    } catch (...) {
        HIKARI_LOG_ERROR("[SceneAsset] registry publication threw an unknown "
                         "exception.");
        if (outError != nullptr) {
            *outError = "Asset registry publication failed with an unknown exception.";
        }
        if (registryPublished) {
            registry_.SwapContents(nextRegistry);
            registrySourceRevision_ = previousRegistrySourceRevision;
        }
        restoreDatabase();
        return false;
    }

    ConfigureModelTextureResolver();
    RenderSubmissionSystem::InvalidateSceneResources(false);
    return true;
}

bool DocumentSceneAssetBindings::PrepareRuntimePrefabAssets(const AssetGuid& prefabGuid,
                                                            const PrefabAssetStore& prefabAssets,
                                                            const SceneRuntimeBuilder& builder,
                                                            const ComponentRegistry& componentRegistry,
                                                            SkyManager& skyManager, std::string& outError) {
    outError.clear();
    const AssetRecord* record = database_.FindByGuid(prefabGuid);
    if (record == nullptr || record->type != AssetType::Prefab || !prefabGuid.IsValid()) {
        outError = "Prefab GUID is missing or belongs to a different asset type: " + prefabGuid.value;
        return false;
    }
    const auto collectDependencies = [&](const PrefabDocument& prefab) {
        SceneDocument prefabScene{};
        prefabScene.objects.reserve(prefab.descendants.size() + 1u);
        prefabScene.objects.push_back(prefab.rootObject);
        prefabScene.objects.insert(prefabScene.objects.end(), prefab.descendants.begin(), prefab.descendants.end());
        return builder.CollectDependencies(prefabScene, &componentRegistry);
    };
    const auto validateAndPreload = [&](const SceneDependencySet& dependencies, const AssetRegistry& registry,
                                        std::string& reason) {
        if (HasMissingSceneDependencyDescriptors(registry, dependencies, &reason)) {
            return false;
        }
        if (!builder.PreloadDependencies(dependencies, registry, models_, skyManager, database_.GetProjectRoot(),
                                         prefabGuid.value, false, &spriteAssets_)) {
            reason = "Prefab dependency preload failed.";
            return false;
        }
        for (const std::string& modelId : dependencies.modelAssetIds) {
            const ModelAsset* model = models_.FindAsset(modelId);
            if (model == nullptr || model->GetState() != ModelAsset::State::Loaded) {
                reason = "Required Prefab model failed to load: " + modelId;
                return false;
            }
        }
        return true;
    };
    if (database_.IsCookedContent()) {
        const std::string artifact =
            ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::PrefabRuntime);
        std::error_code artifactError{};
        if (record->lastImportSucceeded && !artifact.empty() &&
            std::filesystem::is_regular_file(TEXT::PathFromUtf8(artifact), artifactError)) {
            PrefabAssetLoadFailure loadFailure = PrefabAssetLoadFailure::None;
            std::shared_ptr<const PrefabDocument> prefab = prefabAssets.Load(prefabGuid, outError, &loadFailure);
            if (prefab == nullptr) {
                outError = "Cooked Prefab runtime artifact load failed: " + outError;
                return false;
            }
            const SceneDependencySet dependencies = collectDependencies(*prefab);
            return validateAndPreload(dependencies, registry_, outError);
        }
        outError = "Cooked Prefab runtime artifact is unavailable: " + prefabGuid.value;
        return false;
    }
#if !defined(HIKARI_WITH_EDITOR)
    (void)prefabAssets;
    (void)builder;
    (void)componentRegistry;
    (void)skyManager;
    outError = "Source Prefab dependency import is only available in the Editor host.";
    return false;
#else
    const std::shared_ptr<const PrefabDocument> prefab = prefabAssets.LoadSourceForAdmission(prefabGuid, outError);
    if (prefab == nullptr) {
        outError = "Prefab source validation failed: " + outError;
        return false;
    }

    const SceneDependencySet dependencies = collectDependencies(*prefab);
    const auto validatePrepared = [&](const AssetRegistry& preparedRegistry, std::string& reason) {
        return validateAndPreload(dependencies, preparedRegistry, reason);
    };
    if (registrySourceRevision_ == database_.GetContentRevision() &&
        IsImportedAssetClosureReady(database_, prefabGuid) &&
        !HasMissingSceneDependencyDescriptors(registry_, dependencies, &outError)) {
        return validateAndPreload(dependencies, registry_, outError);
    }
    return ReloadAssets(&dependencies, &prefabGuid, validatePrepared, &outError);
#endif
}
void DocumentSceneAssetBindings::ConfigureModelTextureResolver() {
    models_.ResetTextureResolveStats();
    models_.SetTexturePathResolver(
        [this](const std::string& sourceTexturePath, MaterialTextureUsage usage) -> std::string {
            return ResolveModelTexturePath(sourceTexturePath, usage);
        });
}

const AssetRecord* DocumentSceneAssetBindings::FindUniqueTextureAssetByFilename(
    const std::string& filename, const std::string& sourceTexturePath) const {
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
        HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " reason=ambiguous filename matches filename=" + filename +
                        " count=" + std::to_string(matchCount));
        return nullptr;
    }

    return matchCount == 1 ? matchedRecord : nullptr;
}

std::string DocumentSceneAssetBindings::ResolveModelTexturePath(const std::string& sourceTexturePath,
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
        HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " reason=texture asset not found");
        return sourceTexturePath;
    }

    if (record->type != AssetType::Texture || !record->guid.IsValid()) {
        models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
        HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " reason=resolved asset is not a texture");
        return sourceTexturePath;
    }

    const auto* descriptor = registry_.FindAs<TextureAssetDescriptor>(AssetId{record->guid.value});
    if (!descriptor || descriptor->sourcePath.empty()) {
        models_.RecordTextureResolveFailure(ModelTextureResolveFailureKind::Missing);
        HIKARI_LOG_WARN("[ModelTextureResolver] fallback raw texture source=" + sourceTexturePath +
                        " usage=" + MaterialTextureUsageName(usage) + " reason=texture descriptor missing");
        return sourceTexturePath;
    }

    return descriptor->sourcePath;
}

bool DocumentSceneAssetBindings::NeedsRuntimeProgramImport(const SceneDependencySet& required) const {
#if defined(HIKARI_WITH_EDITOR)
    return !CollectRequiredProgramImports(database_, required).empty();
#else
    (void)required;
    return false;
#endif
}

bool DocumentSceneAssetBindings::NeedsRuntimeDependencyRegistryRefresh(const SceneDependencySet& dependencies,
                                                                       std::string* outReason) const {
    if (outReason)
        outReason->clear();
    if (NeedsRuntimeProgramImport(dependencies)) {
        if (outReason)
            *outReason = "Required VFX program is missing or out of date.";
        return true;
    }
    const bool hasDescriptorDependencies =
        !dependencies.modelAssetIds.empty() || !dependencies.skyAssetIds.empty() ||
        !dependencies.textureAssetIds.empty() || !dependencies.materialAssetIds.empty() ||
        !dependencies.modelMaterialSetAssetIds.empty() || !dependencies.spriteAssetIds.empty() ||
        !dependencies.spriteMaterialAssetIds.empty() || !dependencies.vfxAssetIds.empty();
    if (hasDescriptorDependencies && registrySourceRevision_ != database_.GetContentRevision()) {
        if (outReason)
            *outReason = "Asset registry snapshot is out of date.";
        return true;
    }
    return HasMissingAssetDescriptors(registry_, AssetType::Model, dependencies.modelAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::Material, dependencies.materialAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::ModelMaterialBinding, dependencies.modelMaterialSetAssetIds,
                                      outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::Sprite, dependencies.spriteAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::SpriteMaterial, dependencies.spriteMaterialAssetIds,
                                      outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::VfxEffect, dependencies.vfxAssetIds, outReason) ||
           HasMissingAssetDescriptors(registry_, AssetType::Texture, dependencies.textureAssetIds, outReason) ||
           HasMissingSkyResourceDescriptors(registry_, dependencies.skyAssetIds, outReason);
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
    SceneDependencySet deps = scene.CollectRuntimeDependencies();
    if (NeedsRuntimeDependencyRegistryRefresh(deps)) {
        if (!scene.ReloadAssets()) {
            HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before model rebind.");
            return 0;
        }
        deps = scene.CollectRuntimeDependencies();
        if (NeedsRuntimeDependencyRegistryRefresh(deps)) {
            HIKARI_LOG_WARN("[SceneRuntime] model rebind rejected because required "
                            "asset descriptors remain unavailable.");
            return 0;
        }
    }

    scene.state_->runtime.builder.PreloadDependencies(
        deps, registry_, models_, scene.state_->lighting.sky, database_.GetProjectRoot(),
        scene.state_->identity.currentSceneAssetGuid.value, true, &spriteAssets_);
    SKYRENDERER::InvalidateSkyTextureCache();
    RENDER3D::PANORAMA_SKY::InvalidatePanoramaSkyLayerTextureCache();

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

    // Material override は scene の読み込み・更新時だけ再構築する。
    scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
        [this, &rebuiltCount, &materialBuilder](GameObject& object, ModelComponent& modelComponent) {
            modelComponent.ClearRuntimeMaterialOverrides();

            auto& programRegistry = RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
            // 程序化モデルは ModelComponent に保持されないため、
            // レンダリングと同じ「プログラム化対応」の解決を使う。
            const ModelAsset* modelAsset = ResolveRenderableModelAsset(object, modelComponent);
            // プロシージャル形状は ProceduralMeshComponent が
            // マテリアル選択を所有する。
            if (const auto* procedural = object.GetComponent<ProceduralMeshComponent>()) {
                const std::string& selection = procedural->GetMaterialSelection();
                if (!selection.empty()) {
                    const auto* descriptor = registry_.FindAs<MaterialAssetDescriptor>(AssetId{selection});
                    if (descriptor != nullptr) {
                        auto runtimeMaterial = std::make_unique<Material>();
                        if (materialBuilder.BuildRuntimeMaterial(descriptor->sourceData, descriptor->compileResult,
                                                                 registry_, programRegistry, *runtimeMaterial,
                                                                 descriptor->id.value)) {
                            modelComponent.SetRuntimeMaterialOverride(0, std::move(runtimeMaterial));
                            ++rebuiltCount;
                        }
                    } else {
                        HIKARI_LOG_WARN("[MaterialRuntime][Procedural] material "
                                        "selection not registered: " +
                                        selection);
                    }
                }
                return;
            }
            if (modelAsset != nullptr) {
                const auto defaults = ASSETS::MODELS::ResolveModelMaterialBindings(
                    registry_, AssetGuid{modelComponent.GetAssetId()}, modelComponent.GetMaterialSetAssetGuid(),
                    modelAsset->materials, !modelComponent.HasAuthoredMaterialSetSelection());
                for (const std::string& diagnostic : defaults.diagnostics) {
                    HIKARI_LOG_WARN("[MaterialRuntime][ModelDefault] " + diagnostic);
                }
                for (const auto& binding : defaults.bindings) {
                    if (binding.materialDescriptor == nullptr) {
                        continue;
                    }
                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            binding.materialDescriptor->sourceData, binding.materialDescriptor->compileResult,
                            registry_, programRegistry, *runtimeMaterial, binding.materialDescriptor->id.value)) {
                        runtimeMaterial->SetInheritModelTextures(true);
                        modelComponent.SetRuntimeMaterialOverride(binding.materialIndex, std::move(runtimeMaterial));
                        ++rebuiltCount;
                    }
                }
            }

            for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                if (!slot.materialAssetGuid.IsValid()) {
                    continue;
                }

                const auto resolution = ResolveOverrideSlot(modelAsset, slot);
                if (!resolution.has_value()) {
                    continue;
                }

                const auto* descriptor =
                    registry_.FindAs<MaterialAssetDescriptor>(AssetId{slot.materialAssetGuid.value});
                if (!descriptor) {
                    HIKARI_LOG_WARN("[MaterialRuntime] material asset not registered: " + slot.materialAssetGuid.value);
                    continue;
                }

                auto runtimeMaterial = std::make_unique<Material>();
                if (materialBuilder.BuildRuntimeMaterial(descriptor->sourceData, descriptor->compileResult, registry_,
                                                         programRegistry, *runtimeMaterial, descriptor->id.value)) {
                    modelComponent.SetRuntimeMaterialOverride(*resolution, std::move(runtimeMaterial));
                    ++rebuiltCount;
                }
            }
        });

    return rebuiltCount;
}

int DocumentSceneAssetBindings::RebuildMaterialOverridesForMaterial(DocumentSceneBase& scene,
                                                                    const AssetGuid& materialGuid) {
    if (!materialGuid.IsValid()) {
        return 0;
    }

    // A material can be consumed by a model-level default binding as well
    // as a scene instance override. Rebuild the canonical chain so both
    // consumers update together and unavailable assets fall back per slot.
    return RebuildMaterialOverrides(scene);
}

int DocumentSceneAssetBindings::ApplyMaterialOverridePreview(DocumentSceneBase& scene, const AssetGuid& materialGuid,
                                                             const MaterialAssetData& data) {
    if (!materialGuid.IsValid()) {
        return 0;
    }

    MaterialAssetData effectiveData = data;
    MaterialCompileResult effectiveCompile =
        CompileMaterialAsset(effectiveData, ASSETS::MATERIAL::MakeMaterialCompileOptions(database_));
    if (effectiveData.kind == MaterialAssetKind::Instance) {
        ASSETS::MATERIAL::MaterialAssetAuthoringService service(database_);
        const ASSETS::MATERIAL::MaterialInstanceResolveResult resolved = service.ResolveInstance(effectiveData);
        if (!resolved.success) {
            HIKARI_LOG_WARN("[MaterialRuntime][HotPreview] instance composition failed: " + materialGuid.value);
            return 0;
        }
        effectiveData = resolved.resolvedMaterial;
        effectiveCompile = resolved.compile;
    }

    int rebuiltCount = 0;
    MaterialRuntimeBuilder materialBuilder{};
    auto& programRegistry = RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
    scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
        [this, &rebuiltCount, &materialBuilder, &programRegistry, &materialGuid, &effectiveData,
         &effectiveCompile](GameObject& object, ModelComponent& modelComponent) {
            const auto applyPreview = [this, &rebuiltCount, &materialBuilder, &programRegistry, &materialGuid,
                                       &effectiveData, &effectiveCompile,
                                       &modelComponent](uint32_t materialIndex, bool inheritModelTextures) {
                auto runtimeMaterial = std::make_unique<Material>();
                if (materialBuilder.BuildRuntimeMaterial(effectiveData, effectiveCompile, registry_, programRegistry,
                                                         *runtimeMaterial, materialGuid.value)) {
                    runtimeMaterial->SetInheritModelTextures(inheritModelTextures);
                    modelComponent.SetRuntimeMaterialOverride(materialIndex, std::move(runtimeMaterial));
                    ++rebuiltCount;
                } else {
                    modelComponent.ClearRuntimeMaterialOverride(materialIndex);
                }
            };

            struct EffectiveMaterialBinding {
                AssetGuid guid{};
                bool inheritModelTextures = false;
            };
            std::unordered_map<uint32_t, EffectiveMaterialBinding> effectiveMaterialByIndex{};
            const ModelAsset* modelAsset = ResolveRenderableModelAsset(object, modelComponent);
            if (modelAsset != nullptr) {
                const auto materialSet = ASSETS::MODELS::ResolveModelMaterialBindings(
                    registry_, AssetGuid{modelComponent.GetAssetId()}, modelComponent.GetMaterialSetAssetGuid(),
                    modelAsset->materials, !modelComponent.HasAuthoredMaterialSetSelection());
                for (const auto& binding : materialSet.bindings) {
                    if (binding.materialAssetGuid.IsValid()) {
                        effectiveMaterialByIndex[binding.materialIndex] =
                            EffectiveMaterialBinding{binding.materialAssetGuid, true};
                    }
                }
            }

            for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                if (slot.materialAssetGuid != materialGuid) {
                    continue;
                }

                const auto resolution = ResolveOverrideSlot(modelAsset, slot);
                if (!resolution.has_value()) {
                    continue;
                }

                effectiveMaterialByIndex[*resolution] = EffectiveMaterialBinding{slot.materialAssetGuid, false};
            }

            for (const auto& [materialIndex, effectiveBinding] : effectiveMaterialByIndex) {
                if (effectiveBinding.guid == materialGuid) {
                    applyPreview(materialIndex, effectiveBinding.inheritModelTextures);
                }
            }
        });

    return rebuiltCount;
}

int DocumentSceneAssetBindings::ApplyMaterialOverridePreview(DocumentSceneBase& scene, const AssetGuid& materialGuid,
                                                             const PbrMaterialAssetData& data) {
    return ApplyMaterialOverridePreview(scene, materialGuid, MakeStandardPbrMaterialAssetData(data));
}

bool DocumentSceneBase::ReloadAssets() {
    const auto required = CollectRuntimeDependencies();
    const bool reloaded = state_->assets.ReloadAssets(&required);
    if (reloaded) {
        // AssetDatabase retains stable object identity across scans, so
        // pointer equality cannot invalidate cached .hasm definitions.
        // Publish the new registry first, then discard the old cache;
        // failed reloads keep the last-known-good definitions intact.
        state_->runtime.animationStateMachineAssets.Clear();
    }
    return reloaded;
}

bool DocumentSceneBase::RefreshTextureRuntimeByPath(const std::string& path) {
    return state_->assets.RefreshTextureByPath(path);
}

bool DocumentSceneBase::ReloadModelAssetRuntime(const AssetId& modelId) {
    return state_->assets.ReloadModelAsset(modelId);
}

int DocumentSceneBase::RebindModelComponents() { return state_->assets.RebindModelComponents(*this); }

int DocumentSceneBase::RebuildMaterialOverrides() { return state_->assets.RebuildMaterialOverrides(*this); }

int DocumentSceneBase::RebuildMaterialOverridesForMaterial(const AssetGuid& materialGuid) {
    return state_->assets.RebuildMaterialOverridesForMaterial(*this, materialGuid);
}

int DocumentSceneBase::ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid,
                                                           const MaterialAssetData& data) {
    return state_->assets.ApplyMaterialOverridePreview(*this, materialGuid, data);
}

int DocumentSceneBase::ApplyRuntimeMaterialOverridePreview(const AssetGuid& materialGuid,
                                                           const PbrMaterialAssetData& data) {
    return state_->assets.ApplyMaterialOverridePreview(*this, materialGuid, data);
}

} // namespace HIKARI
