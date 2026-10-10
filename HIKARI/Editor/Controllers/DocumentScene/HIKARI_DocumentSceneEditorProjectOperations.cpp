#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"

namespace HIKARI {
namespace {

bool Contains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

nlohmann::json DescribeFeature(const RuntimeFeatureInfo& info, const std::vector<std::string>& enabled) {
    return {{"id", info.featureId},
            {"name", info.displayName},
            {"description", info.description},
            {"requires", info.requiredFeatureIds},
            {"components", info.componentTypeNames},
            {"enabled", Contains(enabled, info.featureId)},
            {"active", info.active}};
}

} // namespace

// 项目の運行时功能（UI、Vfx、Cinematics など）。功能が無いと、その组件と操作（ui.create、特效の放置など）は使えない。
// 「项目功能」面板と同じく、保存したらその場で引擎へ反映する（再起動しない）。
void DocumentSceneEditorController::RegisterProjectOperations() {
    EDITOR::EditorOperationDefinition features{
        "project.features",
        "List the project's runtime features (UI, Vfx, Cinematics, Physics, ...): whether each is enabled, what it "
        "requires and which component types it provides. Components of a disabled feature cannot be added; enable "
        "the feature with project.features.edit.",
        "general",
        {},
        [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "Open a project first.");
            ProjectSettingsService settings;
            if (!settings.Load(aiOperationScene_->GetAssetDatabase().GetProjectRoot()))
                return EDITOR::OperationFailure("SETTINGS_UNREADABLE", "Could not read ProjectSettings.");
            const std::vector<std::string>& enabled = settings.GetSettings().enabledRuntimeFeatures;
            nlohmann::json list = nlohmann::json::array();
            for (const RuntimeFeatureInfo& info : aiOperationScene_->GetRuntimeFeatureCatalog().GetFeatureInfos())
                list.push_back(DescribeFeature(info, enabled));
            return EDITOR::OperationOk("Runtime features.", {{"features", std::move(list)}});
        }};
    features.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    features.searchTerms = {"runtime feature", "enable UI", "enable vfx", "module", "运行时功能", "项目功能"};
    operationRegistry_.Register(std::move(features), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition edit{
        "project.features.edit",
        "Enable or disable runtime features and apply them immediately (no restart). Required features are added "
        "automatically; a feature another enabled feature requires stays enabled. Saves ProjectSettings.",
        "general",
        {{"enable",
          EDITOR::EditorOperationParameterType::Json,
          false,
          "Feature ids to enable, e.g. [\"UI\", \"Vfx\"]",
          {{"type", "array"}, {"items", {{"type", "string"}}}}},
         {"disable",
          EDITOR::EditorOperationParameterType::Json,
          false,
          "Feature ids to disable",
          {{"type", "array"}, {"items", {{"type", "string"}}}}}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "Open a project first.");
            DocumentSceneBase& scene = *aiOperationScene_;
            if (scene.IsRuntimePlayActive())
                return EDITOR::OperationFailure("PLAY_ACTIVE", "Stop Play before changing runtime features.");
            const RuntimeFeatureCatalog& catalog = scene.GetRuntimeFeatureCatalog();
            const std::vector<std::string> known = catalog.GetFeatureIds();
            const auto enable = parameters.value("enable", std::vector<std::string>{});
            const auto disable = parameters.value("disable", std::vector<std::string>{});
            for (const std::vector<std::string>* ids : {&enable, &disable}) {
                for (const std::string& id : *ids) {
                    if (!Contains(known, id))
                        return EDITOR::OperationFailure("UNKNOWN_FEATURE", "Unknown runtime feature: " + id +
                                                                               ". project.features lists them.");
                }
            }
            ProjectSettingsService settings;
            if (!settings.Load(scene.GetAssetDatabase().GetProjectRoot()))
                return EDITOR::OperationFailure("SETTINGS_UNREADABLE", "Could not read ProjectSettings.");
            std::vector<std::string> requested = settings.GetSettings().enabledRuntimeFeatures;
            for (const std::string& id : enable) {
                if (!Contains(requested, id))
                    requested.push_back(id);
            }
            std::erase_if(requested, [&disable](const std::string& id) { return Contains(disable, id); });
            const RuntimeFeatureInstallReport::Resolution resolution = catalog.ResolveEnabledFeatures(requested);
            std::vector<std::string> resolved = resolution.resolvedFeatures;
            // 项目模块などまだ目录に無い功能は消さずに残す。
            for (const std::string& unknown : resolution.unknownFeatures) {
                if (!Contains(resolved, unknown))
                    resolved.push_back(unknown);
            }
            settings.SetEnabledRuntimeFeatures(resolved);
            if (!settings.Save())
                return EDITOR::OperationFailure("SAVE_FAILED", "Could not write ProjectSettings.");
            const bool applied = scene.ReloadRuntimeFeaturesFromProjectSettings();
            nlohmann::json kept = nlohmann::json::array();
            for (const std::string& id : disable) {
                if (Contains(resolved, id))
                    kept.push_back(id);
            }
            nlohmann::json data{{"enabled", resolved},
                                {"addedAsDependencies", resolution.implicitlyEnabledFeatures},
                                {"applied", applied}};
            if (!kept.empty())
                data["keptBecauseRequired"] = std::move(kept);
            if (!applied)
                return {false, "APPLY_FAILED", "Saved, but some features failed to install; see editor logs.",
                        std::move(data)};
            return EDITOR::OperationOk("Runtime features saved and applied.", std::move(data));
        }};
    edit.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    edit.searchTerms = {"enable feature", "runtime feature", "enable UI", "enable vfx", "开启功能", "项目功能"};
    operationRegistry_.Register(std::move(edit), EDITOR::EditorOperationKind::Mutation, false);
}

} // namespace HIKARI
