#include "Editor/Panels/HIKARI_ProjectFeaturesPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureCatalog.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        bool ContainsFeature(
            const std::vector<std::string>& featureIds,
            std::string_view featureId) {

            return std::find(
                featureIds.begin(),
                featureIds.end(),
                featureId) != featureIds.end();
        }

        void AddFeature(
            std::vector<std::string>& featureIds,
            std::string_view featureId) {

            if (!ContainsFeature(featureIds, featureId)) {
                featureIds.emplace_back(featureId);
            }
        }

        void RemoveFeature(
            std::vector<std::string>& featureIds,
            std::string_view featureId) {

            std::erase(featureIds, featureId);
        }

        std::vector<std::string> FindRequiredBy(
            const std::vector<RuntimeFeatureInfo>& infos,
            const std::vector<std::string>& enabledFeatureIds,
            std::string_view featureId) {

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
    }

    void ProjectFeaturesPanel::EnsureLoaded(
        const std::filesystem::path& projectRoot) {

        const std::filesystem::path normalized =
            projectRoot.lexically_normal();
        if (normalized == loadedProjectRoot_) {
            return;
        }
        ReloadDraft(normalized);
    }

    void ProjectFeaturesPanel::ReloadDraft(
        const std::filesystem::path& projectRoot) {

        loadedProjectRoot_ = projectRoot.lexically_normal();
        if (!settingsService_.Load(loadedProjectRoot_)) {
            statusMessage_ =
                "Project settings could not be loaded; defaults are shown.";
            statusIsError_ = true;
        } else {
            statusMessage_ = "Project feature settings loaded.";
            statusIsError_ = false;
        }
        draftFeatureIds_ =
            settingsService_.GetSettings().enabledRuntimeFeatures;
        dirty_ = false;
        draftNeedsNormalization_ = true;
    }

    void ProjectFeaturesPanel::NormalizeDraft(
        const RuntimeFeatureCatalog& catalog) {

        const RuntimeFeatureInstallReport::Resolution resolution =
            catalog.ResolveEnabledFeatures(draftFeatureIds_);
        std::vector<std::string> normalized =
            resolution.resolvedFeatures;
        for (const std::string& unknown : resolution.unknownFeatures) {
            AddFeature(normalized, unknown);
        }
        draftFeatureIds_ = std::move(normalized);
    }

    void ProjectFeaturesPanel::Draw(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        const std::filesystem::path& projectRoot =
            scene.GetAssetDatabase().GetProjectRoot();
        EnsureLoaded(projectRoot);

        const RuntimeFeatureCatalog& catalog =
            scene.GetRuntimeFeatureCatalog();
        if (draftNeedsNormalization_) {
            const std::vector<std::string> loadedFeatureIds =
                draftFeatureIds_;
            NormalizeDraft(catalog);
            if (loadedFeatureIds != draftFeatureIds_) {
                dirty_ = true;
                statusMessage_ =
                    "Required feature dependencies were added to the draft.";
                statusIsError_ = false;
            }
            draftNeedsNormalization_ = false;
        }
        const std::vector<RuntimeFeatureInfo> infos =
            catalog.GetFeatureInfos();
        const bool playActive = scene.IsRuntimePlayActive();

        ImGui::TextWrapped(
            "Choose the runtime modules owned by this project. Dependencies "
            "are enabled automatically; disabled scene data is preserved.");
        if (playActive) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "Stop Play before changing project features.");
        }

        ImGui::SeparatorText("Runtime Features");
        for (const RuntimeFeatureInfo& info : infos) {
            ImGui::PushID(info.featureId.c_str());
            bool enabled = ContainsFeature(
                draftFeatureIds_,
                info.featureId);
            const std::vector<std::string> requiredBy = FindRequiredBy(
                infos,
                draftFeatureIds_,
                info.featureId);
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
            if (locked && ImGui::IsItemHovered(
                    ImGuiHoveredFlags_AllowWhenDisabled)) {
                const std::string owners = JoinNames(requiredBy);
                ImGui::SetTooltip(
                    "Required by: %s",
                    owners.c_str());
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(info.displayName.c_str());
            ImGui::SameLine();
            if (info.active) {
                ImGui::TextColored(
                    ImVec4(0.42f, 0.9f, 0.5f, 1.0f),
                    "Active");
            } else {
                ImGui::TextDisabled("Inactive");
            }
            ImGui::TextDisabled("%s", info.featureId.c_str());
            ImGui::TextWrapped("%s", info.description.c_str());
            if (!info.requiredFeatureIds.empty()) {
                const std::string dependencies =
                    JoinNames(info.requiredFeatureIds);
                ImGui::TextDisabled(
                    "Requires: %s",
                    dependencies.c_str());
            }
            ImGui::Spacing();
            ImGui::PopID();
        }

        const RuntimeFeatureInstallReport::Resolution draftResolution =
            catalog.ResolveEnabledFeatures(draftFeatureIds_);
        if (!draftResolution.unknownFeatures.empty()) {
            ImGui::SeparatorText("Unknown Features");
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "These configured modules are not available in this build.");
            for (const std::string& unknown :
                    draftResolution.unknownFeatures) {
                ImGui::PushID(unknown.c_str());
                ImGui::BulletText("%s", unknown.c_str());
                ImGui::SameLine();
                ImGui::BeginDisabled(playActive);
                if (ImGui::SmallButton("Remove")) {
                    RemoveFeature(draftFeatureIds_, unknown);
                    dirty_ = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }

        const std::vector<RuntimeFeatureSceneIssue> sceneIssues =
            catalog.AnalyzeSceneDocument(scene.GetSceneDocument());
        if (!sceneIssues.empty()) {
            ImGui::SeparatorText("Preserved Inactive Scene Data");
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "Disabled modules are referenced by the current scene. "
                "Their serialized data remains intact.");
            constexpr size_t kMaxVisibleIssues = 12;
            for (size_t index = 0;
                    index < sceneIssues.size() &&
                    index < kMaxVisibleIssues;
                    ++index) {
                const RuntimeFeatureSceneIssue& issue = sceneIssues[index];
                if (issue.kind ==
                    RuntimeFeatureSceneIssueKind::DisabledComponent) {
                    ImGui::BulletText(
                        "%s on %s (%llu) requires %s",
                        issue.itemId.c_str(),
                        issue.objectName.empty()
                            ? "<unnamed>"
                            : issue.objectName.c_str(),
                        static_cast<unsigned long long>(
                            issue.objectId.value),
                        issue.featureId.c_str());
                } else {
                    ImGui::BulletText(
                        "%s requires %s",
                        issue.itemId.c_str(),
                        issue.featureId.c_str());
                }
            }
            if (sceneIssues.size() > kMaxVisibleIssues) {
                ImGui::TextDisabled(
                    "... and %zu more",
                    sceneIssues.size() - kMaxVisibleIssues);
            }
        }

        ImGui::Separator();
        ImGui::BeginDisabled(playActive);
        if (ImGui::Button("Enable All Built-ins")) {
            draftFeatureIds_ = catalog.GetFeatureIds();
            NormalizeDraft(catalog);
            dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            draftFeatureIds_.clear();
            dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload From Disk")) {
            ReloadDraft(projectRoot);
            if (!scene.ReloadRuntimeFeaturesFromProjectSettings()) {
                statusMessage_ =
                    "Settings reloaded, but runtime feature apply reported errors.";
                statusIsError_ = true;
            }
        }

        ImGui::BeginDisabled(!dirty_);
        if (ImGui::Button("Save & Apply")) {
            NormalizeDraft(catalog);
            const bool settingsReloaded =
                settingsService_.Load(projectRoot);
            if (!settingsReloaded) {
                statusMessage_ =
                    "Could not reload project settings before saving.";
                statusIsError_ = true;
            } else {
                settingsService_.SetEnabledRuntimeFeatures(
                    draftFeatureIds_);
            }
            if (!settingsReloaded || !settingsService_.Save()) {
                statusMessage_ = "Could not save project feature settings.";
                statusIsError_ = true;
            } else {
                dirty_ = false;
                const bool applied =
                    scene.ReloadRuntimeFeaturesFromProjectSettings();
                statusMessage_ = applied
                    ? "Project features saved and applied."
                    : "Settings saved, but runtime feature apply reported errors.";
                statusIsError_ = !applied;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled(dirty_ ? "Unsaved changes" : "Saved");
        if (!statusMessage_.empty()) {
            ImGui::TextColored(
                statusIsError_
                    ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f)
                    : ImVec4(0.45f, 0.9f, 0.55f, 1.0f),
                "%s",
                statusMessage_.c_str());
        }
#else
        (void)scene;
#endif
    }

} // namespace HIKARI
