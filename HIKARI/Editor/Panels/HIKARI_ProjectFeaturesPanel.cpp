#include "Editor/Panels/HIKARI_ProjectFeaturesPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectModules.h"
#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI {
namespace {
bool ContainsFeature(const std::vector<std::string>& featureIds, std::string_view featureId) {
    return std::find(featureIds.begin(), featureIds.end(), featureId) != featureIds.end();
}

void AddFeature(std::vector<std::string>& featureIds, std::string_view featureId) {
    if (!ContainsFeature(featureIds, featureId)) {
        featureIds.emplace_back(featureId);
    }
}

void RemoveFeature(std::vector<std::string>& featureIds, std::string_view featureId) {
    std::erase(featureIds, featureId);
}

std::vector<std::string> FindRequiredBy(const std::vector<RuntimeFeatureInfo>& infos,
                                        const std::vector<std::string>& enabledFeatureIds, std::string_view featureId) {
    std::vector<std::string> requiredBy;
    for (const RuntimeFeatureInfo& info : infos) {
        if (!ContainsFeature(enabledFeatureIds, info.featureId) ||
            !ContainsFeature(info.requiredFeatureIds, featureId)) {
            continue;
        }
        requiredBy.push_back(info.displayName);
    }
    return requiredBy;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t index = 0; index < names.size(); ++index) {
        if (index > 0) {
            joined += ", ";
        }
        joined += names[index];
    }
    return joined;
}
} // namespace

void ProjectFeaturesPanel::EnsureLoaded(const std::filesystem::path& projectRoot) {
    const std::filesystem::path normalized = projectRoot.lexically_normal();
    if (normalized == loadedProjectRoot_) {
        return;
    }
    ReloadDraft(normalized);
}

void ProjectFeaturesPanel::ReloadDraft(const std::filesystem::path& projectRoot) {
    loadedProjectRoot_ = projectRoot.lexically_normal();
    if (!settingsService_.Load(loadedProjectRoot_)) {
        statusMessage_ = TEXT::Localize("ui.projectFeatures.loadFailed");
        statusIsError_ = true;
    } else {
        statusMessage_ = TEXT::Localize("ui.projectFeatures.loaded");
        statusIsError_ = false;
    }
    draftFeatureIds_ = settingsService_.GetSettings().enabledRuntimeFeatures;
    moduleListStale_ = true;
    moduleMessage_.clear();
    dirty_ = false;
    draftNeedsNormalization_ = true;
}

void ProjectFeaturesPanel::NormalizeDraft(const RuntimeFeatureCatalog& catalog) {
    const RuntimeFeatureInstallReport::Resolution resolution = catalog.ResolveEnabledFeatures(draftFeatureIds_);
    std::vector<std::string> normalized = resolution.resolvedFeatures;
    for (const std::string& unknown : resolution.unknownFeatures) {
        AddFeature(normalized, unknown);
    }
    draftFeatureIds_ = std::move(normalized);
}

void ProjectFeaturesPanel::Draw(DocumentSceneBase& scene) {
    const std::filesystem::path& projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    EnsureLoaded(projectRoot);

    const RuntimeFeatureCatalog& catalog = scene.GetRuntimeFeatureCatalog();
    if (draftNeedsNormalization_) {
        const std::vector<std::string> loadedFeatureIds = draftFeatureIds_;
        NormalizeDraft(catalog);
        if (loadedFeatureIds != draftFeatureIds_) {
            dirty_ = true;
            statusMessage_ = TEXT::Localize("ui.projectFeatures.dependenciesAdded");
            statusIsError_ = false;
        }
        draftNeedsNormalization_ = false;
    }
    const std::vector<RuntimeFeatureInfo> infos = catalog.GetFeatureInfos();
    const bool playActive = scene.IsRuntimePlayActive();

    ImGui::TextWrapped("%s", TEXT::Localize("ui.projectFeatures.intro").c_str());
    if (playActive) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s",
                           TEXT::Localize("ui.projectFeatures.stopPlayHint").c_str());
    }

    ImGui::SeparatorText(TEXT::Localize("ui.projectFeatures.runtimeFeatures").c_str());
    for (const RuntimeFeatureInfo& info : infos) {
        ImGui::PushID(info.featureId.c_str());
        bool enabled = ContainsFeature(draftFeatureIds_, info.featureId);
        const std::vector<std::string> requiredBy = FindRequiredBy(infos, draftFeatureIds_, info.featureId);
        const bool locked = enabled && !requiredBy.empty();

        ImGui::BeginDisabled(playActive || locked);
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            if (enabled) {
                AddFeature(draftFeatureIds_, info.featureId);
            } else {
                RemoveFeature(draftFeatureIds_, info.featureId);
            }
            NormalizeDraft(catalog);
            dirty_ = true;
        }
        ImGui::EndDisabled();
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            const std::string owners = JoinNames(requiredBy);
            ImGui::SetTooltip(TEXT::Localize("ui.projectFeatures.requiredByFormat").c_str(), owners.c_str());
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(info.displayName.c_str());
        ImGui::SameLine();
        if (info.active) {
            ImGui::TextColored(ImVec4(0.42f, 0.9f, 0.5f, 1.0f), "%s",
                               TEXT::Localize("ui.projectFeatures.active").c_str());
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.projectFeatures.inactive").c_str());
        }
        ImGui::TextDisabled("%s", info.featureId.c_str());
        ImGui::TextWrapped("%s", info.description.c_str());
        if (!info.requiredFeatureIds.empty()) {
            const std::string dependencies = JoinNames(info.requiredFeatureIds);
            ImGui::TextDisabled(TEXT::Localize("ui.projectFeatures.requiresFormat").c_str(), dependencies.c_str());
        }
        ImGui::Spacing();
        ImGui::PopID();
    }

    const RuntimeFeatureInstallReport::Resolution draftResolution = catalog.ResolveEnabledFeatures(draftFeatureIds_);
    if (!draftResolution.unknownFeatures.empty()) {
        ImGui::SeparatorText(TEXT::Localize("ui.projectFeatures.unknownFeatures").c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                           TEXT::Localize("ui.projectFeatures.unknownHint").c_str());
        for (const std::string& unknown : draftResolution.unknownFeatures) {
            ImGui::PushID(unknown.c_str());
            ImGui::BulletText("%s", unknown.c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(playActive);
            if (ImGui::SmallButton(TEXT::Localize("ui.projectFeatures.remove").c_str())) {
                RemoveFeature(draftFeatureIds_, unknown);
                dirty_ = true;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    const std::vector<RuntimeFeatureSceneIssue> sceneIssues = catalog.AnalyzeSceneDocument(scene.GetSceneDocument());
    if (!sceneIssues.empty()) {
        ImGui::SeparatorText(TEXT::Localize("ui.projectFeatures.preservedInactive").c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s",
                           TEXT::Localize("ui.projectFeatures.preservedHint").c_str());
        constexpr size_t kMaxVisibleIssues = 12;
        for (size_t index = 0; index < sceneIssues.size() && index < kMaxVisibleIssues; ++index) {
            const RuntimeFeatureSceneIssue& issue = sceneIssues[index];
            if (issue.kind == RuntimeFeatureSceneIssueKind::DisabledComponent) {
                ImGui::BulletText(TEXT::Localize("ui.projectFeatures.issueComponentFormat").c_str(),
                                  issue.itemId.c_str(),
                                  issue.objectName.empty() ? TEXT::Localize("ui.projectFeatures.unnamed").c_str()
                                                           : issue.objectName.c_str(),
                                  static_cast<unsigned long long>(issue.objectId.value), issue.featureId.c_str());
            } else {
                ImGui::BulletText(TEXT::Localize("ui.projectFeatures.issueRequiresFormat").c_str(),
                                  issue.itemId.c_str(), issue.featureId.c_str());
            }
        }
        if (sceneIssues.size() > kMaxVisibleIssues) {
            ImGui::TextDisabled(TEXT::Localize("ui.projectFeatures.moreFormat").c_str(),
                                sceneIssues.size() - kMaxVisibleIssues);
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(playActive);
    if (ImGui::Button(TEXT::Localize("ui.projectFeatures.enableAll").c_str())) {
        draftFeatureIds_ = catalog.GetFeatureIds();
        NormalizeDraft(catalog);
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.projectFeatures.clearAll").c_str())) {
        draftFeatureIds_.clear();
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.projectFeatures.reloadFromDisk").c_str())) {
        ReloadDraft(projectRoot);
        if (!scene.ReloadRuntimeFeaturesFromProjectSettings()) {
            statusMessage_ = TEXT::Localize("ui.projectFeatures.reloadApplyErrors");
            statusIsError_ = true;
        }
    }

    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button(TEXT::Localize("ui.projectFeatures.saveApply").c_str())) {
        NormalizeDraft(catalog);
        const bool settingsReloaded = settingsService_.Load(projectRoot);
        if (!settingsReloaded) {
            statusMessage_ = TEXT::Localize("ui.projectFeatures.reloadBeforeSaveFailed");
            statusIsError_ = true;
        } else {
            settingsService_.SetEnabledRuntimeFeatures(draftFeatureIds_);
        }
        if (!settingsReloaded || !settingsService_.Save()) {
            statusMessage_ = TEXT::Localize("ui.projectFeatures.saveFailed");
            statusIsError_ = true;
        } else {
            dirty_ = false;
            const bool applied = scene.ReloadRuntimeFeaturesFromProjectSettings();
            statusMessage_ = applied ? TEXT::Localize("ui.projectFeatures.savedApplied")
                                     : TEXT::Localize("ui.projectFeatures.savedApplyErrors");
            statusIsError_ = !applied;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("%s", dirty_ ? TEXT::Localize("ui.projectFeatures.unsaved").c_str()
                                     : TEXT::Localize("ui.projectFeatures.saved").c_str());
    if (!statusMessage_.empty()) {
        ImGui::TextColored(statusIsError_ ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f) : ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "%s",
                           statusMessage_.c_str());
    }

    DrawModules(scene);
}

void ProjectFeaturesPanel::DrawModules(DocumentSceneBase& scene) {
    const ImVec4 errorColor(1.0f, 0.4f, 0.35f, 1.0f);
    const ImVec4 pendingColor(1.0f, 0.72f, 0.25f, 1.0f);
    ImGui::SeparatorText(TEXT::Localize("ui.projectFeatures.modules").c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.projectFeatures.modulesIntro").c_str());
    if (ImGui::Button(TEXT::Localize("ui.projectFeatures.moduleRefresh").c_str())) {
        moduleListStale_ = true;
        moduleMessage_.clear();
    }
    if (moduleListStale_) {
        moduleList_ = EDITOR::BuildProjectModuleList(scene);
        moduleListStale_ = false;
    }
    if (!moduleList_.error.empty()) {
        ImGui::TextColored(errorColor, "%s", moduleList_.error.c_str());
        return;
    }
    if (moduleList_.rows.empty()) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.projectFeatures.moduleNone").c_str());
    }

    bool reopenPending = false;
    for (const EDITOR::ProjectModuleRow& row : moduleList_.rows) {
        const ProjectModuleOption& option = row.option;
        const ModuleManifest& manifest = option.manifest;
        reopenPending = reopenPending || row.reopenPending;
        ImGui::PushID(manifest.id.c_str());
        const bool required = !option.requiredBy.empty();
        bool enabled = option.enabled || required;
        // 他の有効なモジュールが要る包は外せない。使えない包は新しく有効にできない（外すことはできる）。
        ImGui::BeginDisabled(required || (!option.enabled && !option.problem.empty()));
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            if (!SetProjectModuleEnabled(loadedProjectRoot_, GetEngineModulesDirectory(), manifest.id, enabled,
                                         &moduleMessage_)) {
                moduleMessage_ = manifest.id + ": " + moduleMessage_;
            }
            moduleListStale_ = true;
        }
        ImGui::EndDisabled();
        if (required && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(TEXT::Localize("ui.projectFeatures.requiredByFormat").c_str(),
                              JoinNames(option.requiredBy).c_str());
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(manifest.name.empty() ? manifest.id.c_str() : manifest.name.c_str());
        ImGui::SameLine();
        const InstalledModuleStatus* installed = row.installed;
        ImGui::TextDisabled("%s  %s", manifest.version.c_str(),
                            TEXT::Localize(installed == nullptr        ? "ui.projectFeatures.moduleNotLoaded"
                                           : !installed->runtimeLoaded ? "ui.projectFeatures.moduleFailed"
                                           : installed->editorLoaded   ? "ui.projectFeatures.moduleLoadedWithEditor"
                                                                       : "ui.projectFeatures.moduleLoaded")
                                .c_str());
        if (row.reopenPending) {
            ImGui::SameLine();
            ImGui::TextColored(pendingColor, "%s", TEXT::Localize("ui.projectFeatures.moduleReopenPending").c_str());
        }
        ImGui::Indent();
        ImGui::TextDisabled(
            "%s  -  %s", manifest.id.c_str(),
            TEXT::Localize(option.source == ModuleSource::ProjectPlugins ? "ui.projectFeatures.moduleSourceProject"
                           : option.source == ModuleSource::Engine       ? "ui.projectFeatures.moduleSourceEngine"
                                                                         : "ui.projectFeatures.moduleSourcePath")
                .c_str());
        // フォルダの路径は長いので、行を分けて折り返す。
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TEXT::PathToUtf8(manifest.directory).c_str());
        ImGui::PopStyleColor();
        for (const std::string& feature : scene.GetRuntimeExtensionFeatures(manifest.id)) {
            ImGui::TextDisabled("%s", feature.c_str());
        }
        if (!option.problem.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
            ImGui::TextWrapped("%s", option.problem.c_str());
            ImGui::PopStyleColor();
        } else if (installed != nullptr && !installed->error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
            ImGui::TextWrapped("%s", installed->error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (!moduleMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
        ImGui::TextWrapped("%s", moduleMessage_.c_str());
        ImGui::PopStyleColor();
    }
    if (reopenPending) {
        ImGui::TextColored(pendingColor, "%s", TEXT::Localize("ui.projectFeatures.moduleReopenHint").c_str());
    }
}

} // namespace HIKARI
