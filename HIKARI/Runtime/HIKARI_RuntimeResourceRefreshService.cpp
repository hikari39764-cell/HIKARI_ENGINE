#include "HIKARI_RuntimeResourceRefreshService.h"

#include <unordered_set>
#include <utility>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100)
#endif
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace HIKARI {

    namespace {
        bool IsActiveSkyTexture(
            const DocumentSceneBase& scene,
            const std::string& assetId) {
            const SkySettings& sky = scene.GetSceneEnvironment().sky;
            return sky.skyAsset == assetId ||
                sky.panoramaCloudAsset == assetId;
        }
    }

    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshAsset(
        DocumentSceneBase& scene,
        const AssetId& assetId,
        bool assetRegistryAlreadyPublished) {

        RuntimeResourceRefreshReport report{};
        if (!EnsureAssetRegistryPublished(
                scene,
                assetRegistryAlreadyPublished,
                report)) {
            return report;
        }

        const AssetRegistry& registry = scene.GetAssetRegistry();
        if (const auto* texture = registry.FindAs<TextureAssetDescriptor>(assetId)) {
            const std::string textureId = texture->id.value;
            const bool activeSkyTexture =
                IsActiveSkyTexture(scene, textureId);
            if (RefreshTextureAsset(scene, *texture, report)) {
                ReloadCurrentSceneModelDependencies(scene, report);
                if (activeSkyTexture) {
                    scene.RefreshLightingRuntime();
                }
            }
        } else if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(assetId)) {
            RefreshSkyAsset(scene, *sky, report);
        } else if (const auto* model = registry.FindAs<ModelAssetDescriptor>(assetId)) {
            RefreshModelAsset(scene, *model, report, true);
        } else if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(assetId)) {
            RefreshMaterialAsset(scene, *material, report);
        } else {
            ++report.failedCount;
            AppendMessage(report, "Unsupported or missing asset: " + assetId.value);
        }

        return report;
    }

    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshAssets(
        DocumentSceneBase& scene,
        const std::vector<AssetId>& assetIds,
        bool assetRegistryAlreadyPublished) {

        RuntimeResourceRefreshReport report{};
        if (!EnsureAssetRegistryPublished(
                scene,
                assetRegistryAlreadyPublished,
                report)) {
            return report;
        }

        bool textureTouched = false;
        bool skyTextureTouched = false;
        bool modelTouched = false;
        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const AssetId& assetId : assetIds) {
            if (const auto* texture = registry.FindAs<TextureAssetDescriptor>(assetId)) {
                const bool refreshed =
                    RefreshTextureAsset(scene, *texture, report);
                textureTouched = refreshed || textureTouched;
                skyTextureTouched =
                    (refreshed &&
                     IsActiveSkyTexture(scene, texture->id.value)) ||
                    skyTextureTouched;
            } else if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(assetId)) {
                RefreshSkyAsset(scene, *sky, report);
            } else if (const auto* model = registry.FindAs<ModelAssetDescriptor>(assetId)) {
                modelTouched = RefreshModelAsset(scene, *model, report, false) || modelTouched;
            } else if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(assetId)) {
                RefreshMaterialAsset(scene, *material, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Unsupported or missing asset: " + assetId.value);
            }
        }

        if (textureTouched) {
            ReloadCurrentSceneModelDependencies(scene, report);
        } else if (modelTouched) {
            report.modelReboundComponentCount += scene.RebindModelComponents();
        }
        if (skyTextureTouched) {
            scene.RefreshLightingRuntime();
        }

        return report;
    }

    RuntimeResourceRefreshReport RuntimeResourceRefreshService::RefreshCurrentSceneResources(
        DocumentSceneBase& scene,
        bool assetRegistryAlreadyPublished) {

        RuntimeResourceRefreshReport report{};
        if (!EnsureAssetRegistryPublished(
                scene,
                assetRegistryAlreadyPublished,
                report)) {
            return report;
        }

        const SceneDependencySet dependencies =
            scene.GetRuntimeBuilder().CollectDependencies(
                scene.GetSceneDocument(),
                &scene.GetComponentRegistry());

        bool modelTouched = false;
        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const std::string& modelId : dependencies.modelAssetIds) {
            if (const auto* model = registry.FindAs<ModelAssetDescriptor>(AssetId{ modelId })) {
                modelTouched = RefreshModelAsset(scene, *model, report, false) || modelTouched;
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing model dependency: " + modelId);
            }
        }

        if (modelTouched) {
            report.modelReboundComponentCount += scene.RebindModelComponents();
        }

        for (const std::string& skyId : dependencies.skyAssetIds) {
            if (skyId == RENDER3D::LIGHTING::kDefaultSkyAssetName) {
                continue;
            }
            if (const auto* sky = registry.FindAs<SkyAssetDescriptor>(AssetId{ skyId })) {
                RefreshSkyAsset(scene, *sky, report);
            } else if (const auto* texture =
                           registry.FindAs<TextureAssetDescriptor>(
                               AssetId{ skyId })) {
                RefreshTextureAsset(scene, *texture, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing sky dependency: " + skyId);
            }
        }
        std::unordered_set<std::string> refreshedProbeSources{};
        for (const RENDER3D::LIGHTING::ReflectionProbeProvider&
             provider : dependencies.lighting.reflectionProbes) {
            const std::string& probeSourceId =
                provider.sourceCubemapAssetId;
            if (probeSourceId.empty() ||
                !refreshedProbeSources.insert(
                    probeSourceId).second) {
                continue;
            }
            if (dependencies.skyAssetIds.find(probeSourceId) != dependencies.skyAssetIds.end()) {
                continue;
            }
            if (const auto* probeSky = registry.FindAs<SkyAssetDescriptor>(AssetId{ probeSourceId })) {
                scene.RefreshTextureRuntimeByPath(probeSky->prefilteredPath);
                scene.RefreshTextureRuntimeByPath(probeSky->brdfLutPath);
                AppendMessage(report, "Invalidated reflection probe source: " + probeSourceId);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing reflection probe source: " + probeSourceId);
            }
        }

        for (const std::string& materialId : dependencies.materialAssetIds) {
            if (const auto* material = registry.FindAs<MaterialAssetDescriptor>(AssetId{ materialId })) {
                RefreshMaterialAsset(scene, *material, report);
            } else {
                ++report.failedCount;
                AppendMessage(report, "Missing material dependency: " + materialId);
            }
        }

        scene.RefreshLightingRuntime();
        // Runtime refresh 後の environment 状態変化だけを diagnostics log に流す。
        RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
            "RuntimeRefresh.CurrentScene",
            &scene.GetSceneEnvironment());
        return report;
    }

    bool RuntimeResourceRefreshService::EnsureAssetRegistryPublished(
        DocumentSceneBase& scene,
        bool alreadyPublished,
        RuntimeResourceRefreshReport& report) {
        if (alreadyPublished || scene.ReloadAssets()) {
            report.assetRegistryPublished = true;
            return true;
        }
        ++report.failedCount;
        AppendMessage(
            report,
            "Asset registry publication failed before runtime refresh.");
        return false;
    }

    bool RuntimeResourceRefreshService::RefreshTextureAsset(
        DocumentSceneBase& scene,
        const TextureAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        if (descriptor.sourcePath.empty()) {
            ++report.failedCount;
            AppendMessage(report, "Texture descriptor sourcePath is empty: " + descriptor.id.value);
            return false;
        }

        // Texture は cache だけを無効化し、次の model reload / preview 解決時に再取得する。
        if (!scene.RefreshTextureRuntimeByPath(descriptor.sourcePath)) {
            ++report.failedCount;
            AppendMessage(report, "Texture cache invalidation failed: " + descriptor.sourcePath);
            return false;
        }

        ++report.textureInvalidatedCount;
        AppendMessage(report, "Invalidated texture: " + descriptor.sourcePath);
        return true;
    }

    bool RuntimeResourceRefreshService::RefreshSkyAsset(
        DocumentSceneBase& scene,
        const SkyAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        if (descriptor.sourcePath.empty()) {
            ++report.failedCount;
            AppendMessage(report, "Sky descriptor sourcePath is empty: " + descriptor.id.value);
            return false;
        }

        if (scene.GetSceneEnvironment().sky.skyAsset != descriptor.id.value) {
            if (scene.GetSceneEnvironment().reflectionProbe.sourceCubemapAsset != descriptor.id.value) {
                AppendMessage(report, "Sky asset is not active: " + descriptor.id.value);
                return true;
            }
        }

        scene.RefreshTextureRuntimeByPath(descriptor.sourcePath);
        scene.RefreshTextureRuntimeByPath(descriptor.irradiancePath);
        scene.RefreshTextureRuntimeByPath(descriptor.prefilteredPath);
        scene.RefreshTextureRuntimeByPath(descriptor.brdfLutPath);

        // 現在使用中の sky だけを即時差し替える。
        if (!scene.RefreshLightingRuntime()) {
            ++report.failedCount;
            AppendMessage(report, "Lighting runtime refresh failed: " + descriptor.id.value);
            return false;
        }

        ++report.skyInvalidatedCount;
        AppendMessage(report, "Refreshed sky: " + descriptor.id.value);
        // Runtime refresh 後の environment 状態変化だけを diagnostics log に流す。
        RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshotIfChanged(
            "RuntimeRefresh.Sky",
            &scene.GetSceneEnvironment());
        return true;
    }

    bool RuntimeResourceRefreshService::RefreshModelAsset(
        DocumentSceneBase& scene,
        const ModelAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report,
        bool rebindAfterReload) {

        const AssetId modelId = descriptor.id;
        if (!scene.ReloadModelAssetRuntime(modelId)) {
            ++report.failedCount;
            AppendMessage(report, "Failed to reload model: " + modelId.value);
            return false;
        }

        ++report.modelReloadedCount;
        if (rebindAfterReload) {
            report.modelReboundComponentCount += scene.RebindModelComponents();
        }
        AppendMessage(report, "Reloaded model: " + modelId.value);
        return true;
    }

    bool RuntimeResourceRefreshService::RefreshMaterialAsset(
        DocumentSceneBase& scene,
        const MaterialAssetDescriptor& descriptor,
        RuntimeResourceRefreshReport& report) {

        const int rebuilt = scene.RebuildMaterialOverridesForMaterial(
            AssetGuid{ descriptor.id.value });
        ++report.materialReloadedCount;
        report.materialReboundComponentCount += rebuilt;
        AppendMessage(report, "Rebuilt material override: " + descriptor.id.value);
        return true;
    }

    void RuntimeResourceRefreshService::ReloadCurrentSceneModelDependencies(
        DocumentSceneBase& scene,
        RuntimeResourceRefreshReport& report) {

        const SceneDependencySet dependencies =
            scene.GetRuntimeBuilder().CollectDependencies(
                scene.GetSceneDocument(),
                &scene.GetComponentRegistry());

        bool modelTouched = false;
        const AssetRegistry& registry = scene.GetAssetRegistry();
        for (const std::string& modelId : dependencies.modelAssetIds) {
            if (const auto* model = registry.FindAs<ModelAssetDescriptor>(AssetId{ modelId })) {
                modelTouched = RefreshModelAsset(scene, *model, report, false) || modelTouched;
            }
        }

        if (modelTouched) {
            report.modelReboundComponentCount += scene.RebindModelComponents();
        }
    }

    void RuntimeResourceRefreshService::AppendMessage(
        RuntimeResourceRefreshReport& report,
        std::string message) const {

        HIKARI_LOG_INFO("[RuntimeRefresh] " + message);
        report.messages.push_back(std::move(message));
    }

} // namespace HIKARI
