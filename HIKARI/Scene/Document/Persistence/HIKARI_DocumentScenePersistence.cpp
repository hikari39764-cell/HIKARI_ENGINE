#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <filesystem>
#include <algorithm>
#include <vector>
#include <utility>

#include "HIKARI_Services.h"
#include "Core/HIKARI_Logger.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    bool DocumentSceneBase::RequestOpenSceneAsset(const AssetGuid& sceneGuid) {
        return OpenSceneAssetNow(sceneGuid);
    }
    bool DocumentSceneBase::OpenSceneAssetNow(const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            return false;
        }
        if (!SERVICES::IsRuntimeSceneGuidAllowed(sceneGuid.value)) {
            HIKARI_LOG_WARN("[SceneAsset] blocked scene outside runtime export set: " + sceneGuid.value);
            return false;
        }
        if (state_->assets.Database().GetProjectRoot().empty()) {
            state_->assets.Database().Initialize(std::filesystem::current_path());
        }

        const AssetRecord* record = state_->assets.Database().FindByGuid(sceneGuid);
        if (!record) {
            state_->assets.Database().ScanAssets(false);
            record = state_->assets.Database().FindByGuid(sceneGuid);
        }
        if (!record || record->type != AssetType::Scene || record->sourcePath.empty()) {
            return false;
        }

        const std::filesystem::path scenePath =
            (state_->assets.Database().GetProjectRoot() / record->sourcePath).lexically_normal();
        SceneDocument loaded{};
        if (!state_->identity.serializer.LoadFromFile(scenePath.generic_string(), loaded)) {
            return false;
        }

        if (!state_->runtime.playActive) {
            EndEditorCameraPreview();
        }
        state_->identity.document = std::move(loaded);
        ++state_->identity.documentRevision;
        state_->identity.scenePath = scenePath.generic_string();
        state_->identity.sceneId = sceneGuid.value;
        state_->identity.currentSceneAssetGuid = sceneGuid;
        state_->identity.documentDirty = false;

        state_->lighting.environment = state_->identity.document.environment;
        ReloadAssets();
        return RebuildRuntimeWorld();
    }
    bool DocumentSceneBase::OpenStartupSceneAsset() {
        if (state_->assets.Database().GetProjectRoot().empty()) {
            state_->assets.Database().Initialize(std::filesystem::current_path());
        }

        // ProjectSettings 邵ｺ・ｮ GUID 郢ｧ雋樞煤陷亥現・邵ｲ竏ｵ謔ｴ髫ｪ・ｭ陞ｳ螢ｹ竊醍ｹｧ逕ｻ諤呵崕譏ｴ繝ｻ Scene Asset 郢ｧ蜻域ｲｻ騾包ｽｨ邵ｺ蜷ｶ・狗ｸｲ繝ｻ
        state_->assets.Database().ScanAssets(true);

        const std::string& runtimeStartupSceneGuid = SERVICES::GetRuntimeStartupSceneGuid();
        if (!runtimeStartupSceneGuid.empty() && OpenSceneAssetNow(AssetGuid{ runtimeStartupSceneGuid })) {
            return true;
        }

        ProjectSettingsService settings{};
        settings.Load(state_->assets.Database().GetProjectRoot());

        const AssetGuid startupGuid = settings.GetSettings().startupSceneGuid;
        if (startupGuid.IsValid() &&
            SERVICES::IsRuntimeSceneGuidAllowed(startupGuid.value) &&
            OpenSceneAssetNow(startupGuid)) {
            return true;
        }

        std::vector<const AssetRecord*> sceneRecords = state_->assets.Database().CollectByType(AssetType::Scene);
        std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
            if (!lhs || !rhs) {
                return lhs < rhs;
            }
            return lhs->sourcePath.generic_string() < rhs->sourcePath.generic_string();
        });

        for (const AssetRecord* record : sceneRecords) {
            if (!record || !record->guid.IsValid()) {
                continue;
            }
            if (!SERVICES::IsRuntimeSceneGuidAllowed(record->guid.value)) {
                continue;
            }
            if (SERVICES::IsEditorHost()) {
                settings.SetStartupSceneGuid(record->guid);
                settings.Save();
            }
            return OpenSceneAssetNow(record->guid);
        }

        return false;
    }
    bool DocumentSceneBase::CreateTransientEmptySceneDocument() {
        EndEditorCameraPreview();
        state_->camera.sequencePlayback.Reset();
        state_->camera.currentCameraSequenceHandle = {};
        state_->camera.director.Reset();
        state_->camera.rigService.Clear();
        state_->identity.document = SceneDocument{};
        ++state_->identity.documentRevision;
        state_->identity.document.sceneName = "Untitled Scene";
        state_->identity.document.systems =
            state_->runtime.featureCatalog.CreateDefaultSceneSystems();
        state_->lighting.environment = state_->identity.document.environment;
        state_->identity.scenePath.clear();
        state_->identity.sceneId = "TransientScene";
        state_->identity.currentSceneAssetGuid = {};
        state_->identity.documentDirty = false;
        RenderSubmissionSystem::InvalidateSceneResources(true);
        state_->camera.director.SetBaseCamera({});
        return true;
    }
    bool DocumentSceneBase::HasUnsavedSceneChanges() const {
        return state_->identity.documentDirty;
    }
    void DocumentSceneBase::SetUnsavedSceneChanges(bool dirty) {
        state_->identity.documentDirty = dirty;
    }
    bool DocumentSceneBase::SaveCurrentSceneDocument() {
        if (!state_->identity.currentSceneAssetGuid.IsValid() || state_->identity.scenePath.empty()) {
            return false;
        }

        state_->identity.document.environment = state_->lighting.environment;
        const bool saved = state_->identity.serializer.SaveToFile(state_->identity.scenePath, state_->identity.document);
        if (saved) {
            state_->identity.documentDirty = false;
        }
        return saved;
    }
    bool DocumentSceneBase::SaveCurrentSceneDocumentAs(const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            return false;
        }
        if (state_->assets.Database().GetProjectRoot().empty()) {
            state_->assets.Database().Initialize(std::filesystem::current_path());
        }

        const AssetRecord* record = state_->assets.Database().FindByGuid(sceneGuid);
        if (!record) {
            state_->assets.Database().ScanAssets(false);
            record = state_->assets.Database().FindByGuid(sceneGuid);
        }
        if (!record || record->type != AssetType::Scene || record->sourcePath.empty()) {
            return false;
        }

        const std::filesystem::path absolutePath =
            (state_->assets.Database().GetProjectRoot() / record->sourcePath).lexically_normal();
        state_->identity.document.environment = state_->lighting.environment;
        if (!state_->identity.serializer.SaveToFile(absolutePath.generic_string(), state_->identity.document)) {
            return false;
        }

        state_->identity.scenePath = absolutePath.generic_string();
        state_->identity.sceneId = sceneGuid.value;
        state_->identity.currentSceneAssetGuid = sceneGuid;
        state_->identity.documentDirty = false;
        return true;
    }
    const AssetGuid& DocumentSceneBase::GetCurrentSceneAssetGuid() const {
        return state_->identity.currentSceneAssetGuid;
    }
    bool DocumentSceneBase::IsCurrentSceneAsset(const AssetGuid& guid) const {
        return state_->identity.currentSceneAssetGuid.IsValid() && state_->identity.currentSceneAssetGuid == guid;
    }
    std::string DocumentSceneBase::GetCurrentSceneDisplayName() const {
        if (state_->identity.currentSceneAssetGuid.IsValid()) {
            if (const AssetRecord* record = state_->assets.Database().FindByGuid(state_->identity.currentSceneAssetGuid)) {
                if (!record->displayName.empty()) {
                    return record->displayName;
                }
            }
        }
        return state_->identity.document.sceneName;
    }

} // namespace HIKARI
