#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Assets/HIKARI_DocumentSceneAssetBindings.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <filesystem>
#include <memory>
#include <unordered_set>
#include <utility>

#include "Render2D/HIKARI_DxTexture.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    namespace {
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
        if (database_.GetProjectRoot().empty()) {
            database_.Initialize(std::filesystem::current_path());
        }
        const bool okDatabase = database_.ScanAssets(true);

        // AssetDatabase 郢ｧ雋樣ｫｪ闕ｳﾂ邵ｺ・ｮ騾具ｽｻ鬪ｭ・ｲ陷医・竊堤ｸｺ蜉ｱ窶ｻ runtime descriptor 郢ｧ蜑・ｽｽ諛奇ｽ企ｶ・ｴ邵ｺ蜷ｶﾂ繝ｻ
        registry_.Clear();
        AssetRegistryBuilder assetRegistryBuilder{};
        const bool okRegistry = assetRegistryBuilder.AppendToRegistry(database_, registry_);

        ConfigureModelTextureResolver();
        RenderSubmissionSystem::InvalidateSceneResources(false);
        return okDatabase && okRegistry;
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
        return
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
                AssetType::Sky,
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
            if (ReloadAssets()) {
                deps = scene.state_->runtime.builder.CollectDependencies(
                    scene.state_->identity.document,
                    &scene.state_->runtime.componentRegistry);
            } else {
                HIKARI_LOG_WARN("[SceneRuntime] asset registry refresh failed before model rebind.");
            }
        }

        scene.state_->runtime.builder.PreloadDependencies(
            deps,
            registry_,
            models_,
            scene.state_->lighting.sky,
            database_.GetProjectRoot(),
            scene.state_->identity.currentSceneAssetGuid.value);

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
            [this, &rebuiltCount, &materialBuilder](GameObject&, ModelComponent& modelComponent) {
                modelComponent.ClearRuntimeMaterialOverride();
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || !slot.materialAssetGuid.IsValid()) {
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
                            descriptor->data,
                            registry_,
                            *runtimeMaterial,
                            descriptor->id.value)) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            slot.materialAssetGuid);
                        ++rebuiltCount;
                    }
                    break;
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

        const auto* descriptor = registry_.FindAs<MaterialAssetDescriptor>(
            AssetId{ materialGuid.value });
        if (!descriptor) {
            HIKARI_LOG_WARN("[MaterialRuntime] material asset not registered: " + materialGuid.value);
            return 0;
        }

        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};
        scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder, &materialGuid, descriptor](GameObject&, ModelComponent& modelComponent) {
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || slot.materialAssetGuid != materialGuid) {
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            descriptor->data,
                            registry_,
                            *runtimeMaterial,
                            descriptor->id.value)) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            materialGuid);
                        ++rebuiltCount;
                    }
                    break;
                }
            });

        return rebuiltCount;
    }

    int DocumentSceneAssetBindings::ApplyMaterialOverridePreview(
        DocumentSceneBase& scene,
        const AssetGuid& materialGuid,
        const PbrMaterialAssetData& data) {

        if (!materialGuid.IsValid()) {
            return 0;
        }

        int rebuiltCount = 0;
        MaterialRuntimeBuilder materialBuilder{};
        scene.state_->runtime.world.ForEachObjectWith<ModelComponent>(
            [this, &rebuiltCount, &materialBuilder, &materialGuid, &data](GameObject&, ModelComponent& modelComponent) {
                for (const ModelMaterialOverrideSlot& slot : modelComponent.GetMaterialOverrides()) {
                    if (slot.slotIndex != 0 || slot.materialAssetGuid != materialGuid) {
                        continue;
                    }

                    auto runtimeMaterial = std::make_unique<Material>();
                    if (materialBuilder.BuildRuntimeMaterial(
                            data,
                            registry_,
                            *runtimeMaterial,
                            materialGuid.value + "/preview")) {
                        modelComponent.SetRuntimeMaterialOverride(
                            std::move(runtimeMaterial),
                            materialGuid);
                        ++rebuiltCount;
                    }
                    break;
                }
            });

        return rebuiltCount;
    }

    bool DocumentSceneBase::ReloadAssets() {
        return state_->assets.ReloadAssets();
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
        const PbrMaterialAssetData& data) {
        return state_->assets.ApplyMaterialOverridePreview(
            *this,
            materialGuid,
            data);
    }


} // namespace HIKARI
