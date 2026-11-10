#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectModules.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/Project/HIKARI_ProjectModuleList.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"

namespace HIKARI {
namespace {

bool Contains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

constexpr TEXT::EnumName<ModuleSource> kModuleSourceNames[] = {
    {ModuleSource::ProjectPlugins, "project"},
    {ModuleSource::Engine, "engine"},
    {ModuleSource::ManifestPath, "path"},
};

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
        "List the project's runtime features (Vfx, Cinematics, Physics, ...): whether each is enabled, what it "
        "requires and which component types it provides. Components of a disabled feature cannot be added; enable "
        "the feature with project.features.edit. Module packages are listed by project.modules.",
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
    features.searchTerms = {"runtime feature", "enable UI", "enable vfx", "运行时功能", "项目功能"};
    operationRegistry_.Register(std::move(features), EDITOR::EditorOperationKind::Query, false);

    // 挿件包（外部モジュール）。項目の Plugins フォルダか引擎に同梱のフォルダにある包を、Project.hikari の参照で
    // 有効にする。DLL の読み込みは項目を開く時だけなので、変更は開き直すまで反映されない。
    EDITOR::EditorOperationDefinition modules{
        "project.modules",
        "List the module packages this project can use: the ones in the project's Plugins folder, the ones shipped "
        "with the engine and the ones Project.hikari references by path. Each entry tells whether the project "
        "enables it, whether it is loaded in this session, the features it provides, and any problem (unreadable "
        "manifest, built for another SDK version, missing dependency). reopenPending is true when Project.hikari "
        "and the loaded modules differ; reopen the project to apply. Enable or disable with "
        "project.modules.set_enabled.",
        "general",
        {},
        [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "Open a project first.");
            const EDITOR::ProjectModuleList list = EDITOR::BuildProjectModuleList(*aiOperationScene_);
            if (!list.error.empty())
                return EDITOR::OperationFailure("PROJECT_UNREADABLE", list.error);
            nlohmann::json entries = nlohmann::json::array();
            bool reopenPending = false;
            for (const EDITOR::ProjectModuleRow& row : list.rows) {
                const ModuleManifest& manifest = row.option.manifest;
                reopenPending = reopenPending || row.reopenPending;
                entries.push_back({{"id", manifest.id},
                                   {"name", manifest.name},
                                   {"version", manifest.version},
                                   {"sdkVersion", manifest.sdkVersion},
                                   {"source", TEXT::EnumToName(kModuleSourceNames, row.option.source)},
                                   {"directory", TEXT::PathToUtf8(manifest.directory)},
                                   {"enabled", row.option.enabled},
                                   {"requiredBy", row.option.requiredBy},
                                   {"problem", row.option.problem},
                                   {"runtimeLoaded", row.installed != nullptr && row.installed->runtimeLoaded},
                                   {"editorLoaded", row.installed != nullptr && row.installed->editorLoaded},
                                   {"loadError", row.installed != nullptr ? row.installed->error : std::string{}},
                                   {"features", aiOperationScene_->GetRuntimeExtensionFeatures(manifest.id)},
                                   {"reopenPending", row.reopenPending}});
            }
            return EDITOR::OperationOk("Module packages.",
                                       {{"modules", std::move(entries)}, {"reopenPending", reopenPending}});
        }};
    modules.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    modules.searchTerms = {"module", "plugin", "package", "插件", "模块", "插件包"};
    operationRegistry_.Register(std::move(modules), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition moduleEnabled{
        "project.modules.set_enabled",
        "Enable or disable a module package for this project by writing Project.hikari. Module libraries load only "
        "when a project opens, so the change takes effect after the project is reopened. A package with a problem "
        "cannot be enabled, and a module another enabled module depends on cannot be disabled.",
        "general",
        {{"module", EDITOR::EditorOperationParameterType::String, true, "Module id from project.modules"},
         {"enabled", EDITOR::EditorOperationParameterType::Bool, true, "True enables the module, false disables it"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "Open a project first.");
            const std::string id = parameters.value("module", std::string{});
            const bool enabled = parameters.value("enabled", false);
            std::string message{};
            if (!SetProjectModuleEnabled(aiOperationScene_->GetAssetDatabase().GetProjectRoot(),
                                         GetEngineModulesDirectory(), id, enabled, &message))
                return EDITOR::OperationFailure("MODULE_NOT_CHANGED", message);
            return EDITOR::OperationOk(std::string(enabled ? "Enabled " : "Disabled ") + id +
                                           ". Reopen the project to apply.",
                                       {{"module", id}, {"enabled", enabled}, {"reopenRequired", true}});
        }};
    moduleEnabled.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    moduleEnabled.searchTerms = {"enable module", "install plugin", "启用模块", "安装插件"};
    operationRegistry_.Register(std::move(moduleEnabled), EDITOR::EditorOperationKind::Mutation, false);

    EDITOR::EditorOperationDefinition moduleExtensions{
        "module.extensions",
        "List the viewport tools and windows that installed modules add to the Scene workspace, with which tool is "
        "active and which windows are open. Operations registered by modules appear in the normal operation list.",
        "general",
        {},
        [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
            if (projectWorkspaceHost_ == nullptr)
                return EDITOR::OperationFailure("NO_MODULES", "No editor modules are loaded.");
            const EDITOR::ModuleEditorExtensions& extensions = projectWorkspaceHost_->Extensions();
            const EDITOR::ModuleEditorExtensions::Tool* active = extensions.GetActiveTool();
            nlohmann::json tools = nlohmann::json::array();
            for (const EDITOR::ModuleEditorExtensions::Tool& tool : extensions.GetTools()) {
                tools.push_back({{"id", tool.id},
                                 {"module", tool.moduleId},
                                 {"name", tool.name},
                                 {"active", active != nullptr && active->id == tool.id}});
            }
            nlohmann::json panels = nlohmann::json::array();
            for (const EDITOR::ModuleEditorExtensions::Panel& panel : extensions.GetPanels()) {
                panels.push_back(
                    {{"id", panel.id}, {"module", panel.moduleId}, {"name", panel.name}, {"open", panel.open}});
            }
            return EDITOR::OperationOk("Module editor extensions.",
                                       {{"tools", std::move(tools)}, {"panels", std::move(panels)}});
        }};
    moduleExtensions.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    moduleExtensions.searchTerms = {"module tool", "module panel", "viewport tool", "模块工具", "模块窗口"};
    operationRegistry_.Register(std::move(moduleExtensions), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition moduleTool{
        "module.tool.activate",
        "Make a module's viewport tool the active one, so it takes the mouse in the scene viewport before selection. "
        "An empty id puts the current tool away.",
        "general",
        {{"tool", EDITOR::EditorOperationParameterType::String, true,
          "Tool id from module.extensions; empty to clear"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (projectWorkspaceHost_ == nullptr)
                return EDITOR::OperationFailure("NO_MODULES", "No editor modules are loaded.");
            const std::string tool = parameters.value("tool", std::string{});
            if (!projectWorkspaceHost_->Extensions().ActivateTool(tool))
                return EDITOR::OperationFailure("UNKNOWN_TOOL", "Unknown module tool: " + tool);
            return EDITOR::OperationOk(tool.empty() ? "No module tool is active." : "Activated: " + tool, {});
        }};
    moduleTool.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
    operationRegistry_.Register(std::move(moduleTool), EDITOR::EditorOperationKind::Session, false);

    EDITOR::EditorOperationDefinition modulePanel{
        "module.panel.set_open",
        "Open or close a window added by an installed module.",
        "general",
        {{"panel", EDITOR::EditorOperationParameterType::String, true, "Window id from module.extensions"},
         {"open", EDITOR::EditorOperationParameterType::Bool, true, "True opens the window, false closes it"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (projectWorkspaceHost_ == nullptr)
                return EDITOR::OperationFailure("NO_MODULES", "No editor modules are loaded.");
            const std::string panel = parameters.value("panel", std::string{});
            if (!projectWorkspaceHost_->Extensions().SetPanelOpen(panel, parameters.value("open", false)))
                return EDITOR::OperationFailure("UNKNOWN_PANEL", "Unknown module window: " + panel);
            return EDITOR::OperationOk("Window state changed.", {});
        }};
    modulePanel.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
    operationRegistry_.Register(std::move(modulePanel), EDITOR::EditorOperationKind::Session, false);

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
