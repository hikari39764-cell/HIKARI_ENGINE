#include "Core/Text/HIKARI_Localization.h"
#include "Editor/SystemAuthoring/HIKARI_SystemSettingsInspector.h"

#include <algorithm>
#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/SystemAuthoring/HIKARI_SystemSettingsFieldRenderer.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

bool EqualSystemData(const SceneSystemData& left, const SceneSystemData& right) {
    return left.systemId == right.systemId && left.enabled == right.enabled &&
           left.executionOrder == right.executionOrder && left.settings == right.settings;
}

#if defined(HIKARI_WITH_EDITOR)
const char* InstallationLabel(const SceneSystemAuthoringRow& row) noexcept {
    if (row.projectDefault) {
        return "Project";
    }
    if (IsSceneSystemAutomatic(row)) {
        return "Automatic";
    }
    if (row.requiredComponentCount > 0) {
        return "Override";
    }
    return "Scene";
}

void DrawSourceBadge(const SceneSystemAuthoringRow& row) {
    const ImVec4 color = row.projectDefault            ? ImVec4(0.45f, 0.72f, 0.95f, 1.0f)
                         : IsSceneSystemAutomatic(row) ? ImVec4(0.58f, 0.82f, 0.62f, 1.0f)
                                                       : ImVec4(0.72f, 0.62f, 0.92f, 1.0f);
    ImGui::TextColored(color, "%s", InstallationLabel(row));
}
#endif
} // namespace

void SystemSettingsInspector::Select(SceneSystemData initial, std::string displayName) {
    original_ = initial;
    draft_ = std::move(initial);
    displayName_ = std::move(displayName);
    validationIssues_.clear();
    hasTarget_ = true;
}

void SystemSettingsInspector::Clear() {
    hasTarget_ = false;
    original_ = {};
    draft_ = {};
    displayName_.clear();
    validationIssues_.clear();
}

void SystemSettingsInspector::Revert() {
    draft_ = original_;
    validationIssues_.clear();
}

bool SystemSettingsInspector::HasTarget() const noexcept { return hasTarget_; }

bool SystemSettingsInspector::IsDirty() const noexcept { return hasTarget_ && !EqualSystemData(original_, draft_); }

const std::string& SystemSettingsInspector::GetTargetSystemId() const noexcept { return draft_.systemId; }

SystemSettingsInspectorResult SystemSettingsInspector::Draw(const SceneSystemAuthoringRow& row,
                                                            DocumentSceneBase& scene,
                                                            const SystemTypeRegistry& runtimeRegistry,
                                                            const SystemAuthoringRegistry& authoringRegistry,
                                                            EditorToolHost& toolHost, bool readOnly) {
    SystemSettingsInspectorResult result{};
#if defined(HIKARI_WITH_EDITOR)
    if (!hasTarget_) {
        return result;
    }

    const SystemTypeInfo* runtimeInfo = runtimeRegistry.Find(draft_.systemId);
    const SystemAuthoringDescriptor* authoring = authoringRegistry.Find(draft_.systemId);

    ImGui::TextUnformatted(displayName_.c_str());
    ImGui::SameLine();
    DrawSourceBadge(row);
    if (readOnly) {
        ImGui::SameLine();
        ImGui::TextDisabled(TEXT::Localize("editor.ui.play.locked.66c28e").c_str());
    }
    ImGui::Separator();

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    (void)ImGui::BeginChild("SystemInspectorBody", ImVec2(0.0f, -footerHeight), false);
    ImGui::BeginDisabled(readOnly);
    ImGui::Checkbox(TEXT::LocalizeWindow("ui.vfx.text.Enabled", "Enabled").c_str(), &draft_.enabled);

    const bool hasRegularFields =
        authoring && std::any_of(authoring->fields.begin(), authoring->fields.end(),
                                 [](const SystemSettingField& field) { return !field.advanced; });
    const bool hasAdvancedFields =
        authoring && std::any_of(authoring->fields.begin(), authoring->fields.end(),
                                 [](const SystemSettingField& field) { return field.advanced; });
    const bool hasCustomSettings = authoring && authoring->drawCustomSettings;

    if (hasRegularFields || hasCustomSettings) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.settings.c7f73b").c_str());
        if (hasRegularFields) {
            (void)DrawSystemSettingsFields(authoring->fields, draft_.settings, false);
        }
        if (hasCustomSettings) {
            SystemSettingsEditorContext context{scene, draft_.systemId};
            (void)authoring->drawCustomSettings(context, draft_.settings);
        }
    }

    if (authoring && !authoring->dedicatedToolId.empty()) {
        ImGui::Spacing();
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.open.tool.59e596", "Open Tool...").c_str())) {
            EditorToolOpenRequest request{};
            request.toolId = authoring->dedicatedToolId;
            toolHost.RequestOpen(std::move(request));
        }
    }

    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("ui.vfx.layer.advanced", "Advanced").c_str())) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragInt(TEXT::LocalizeWindow("editor.ui.execution.order.17e703", "Execution Order").c_str(),
                       &draft_.executionOrder, 1.0f, -10000, 10000);
        if (hasAdvancedFields) {
            (void)DrawSystemSettingsFields(authoring->fields, draft_.settings, true);
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.reset.settings.e498b8", "Reset Settings").c_str())) {
            draft_.settings = runtimeRegistry.CreateDefaultSettings(draft_.systemId);
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.input.restoreDefaults", "Restore Defaults").c_str())) {
            draft_.enabled = true;
            draft_.executionOrder = row.defaultExecutionOrder;
            draft_.settings = runtimeRegistry.CreateDefaultSettings(draft_.systemId);
        }
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("shell.diagnostics", "Diagnostics").c_str())) {
        ImGui::Text("ID: %s", draft_.systemId.c_str());
        ImGui::Text(TEXT::Localize("editor.ui.feature.s.edca76").c_str(),
                    runtimeInfo && !runtimeInfo->featureId.empty() ? runtimeInfo->featureId.c_str() : "<none>");
        ImGui::Text(TEXT::Localize("ui.assetBrowser.tooltipSource").c_str(), InstallationLabel(row));
        ImGui::Text(TEXT::Localize("editor.ui.scheduled.s.60f078").c_str(), row.scheduled ? "Yes" : "No");
        if (row.requiredComponentCount > 0) {
            ImGui::Text(TEXT::Localize("editor.ui.component.references.zu.230c91").c_str(), row.requiredComponentCount);
            for (const std::string& componentType : row.requiredComponentTypes) {
                ImGui::BulletText("%s", componentType.c_str());
            }
        }
        if (authoring) {
            ImGui::Text(TEXT::Localize("editor.ui.settings.owner.s.66f6aa").c_str(), ToDisplayName(authoring->scope));
        }
    }

    nlohmann::json preparedSettings{};
    validationIssues_.clear();
    const bool settingsValid =
        runtimeInfo != nullptr &&
        runtimeRegistry.PrepareSettings(draft_.systemId, draft_.settings, preparedSettings, &validationIssues_);
    if (!settingsValid) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.issue.73781a").c_str());
        if (validationIssues_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                               TEXT::Localize("editor.ui.runtime.type.unavailable.500aff").c_str());
        } else {
            for (const std::string& issue : validationIssues_) {
                ImGui::BulletText("%s", issue.c_str());
            }
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    const bool mayRemove = row.hasDocumentEntry && !row.projectDefault;
    if (mayRemove) {
        const char* removeLabel = row.requiredComponentCount > 0 ? "Use Automatic" : "Remove System";
        ImGui::BeginDisabled(readOnly);
        if (ImGui::Button(removeLabel)) {
            ImGui::OpenPopup("Confirm System Removal###SystemInspectorRemove");
        }
        ImGui::EndDisabled();
    }

    const float actionWidth = 96.0f;
    const float actionsWidth = actionWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - actionsWidth));
    ImGui::BeginDisabled(!IsDirty() || readOnly);
    if (ImGui::Button(TEXT::LocalizeWindow("ui.assetInspector.revert", "Revert").c_str(), ImVec2(actionWidth, 0.0f))) {
        Revert();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!IsDirty() || !settingsValid || readOnly);
    if (ImGui::Button(TEXT::LocalizeWindow("ui.common.apply", "Apply").c_str(), ImVec2(actionWidth, 0.0f))) {
        draft_.settings = std::move(preparedSettings);
        original_ = draft_;
        result.action = SystemSettingsInspectorAction::Apply;
        result.system = draft_;
    }
    ImGui::EndDisabled();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 150.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.confirm.system.removal.ba019b", "SystemInspectorRemove").c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s %s?", row.requiredComponentCount > 0 ? "Return" : "Remove", displayName_.c_str());
        if (row.requiredComponentCount > 0) {
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.component.requirements.will.keep.it.installed.automatically.123474").c_str());
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.confirm.04a212", "Confirm").c_str(), ImVec2(96.0f, 0.0f))) {
            result.action = SystemSettingsInspectorAction::RemoveOverride;
            result.system = draft_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(96.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)row;
    (void)scene;
    (void)runtimeRegistry;
    (void)authoringRegistry;
    (void)toolHost;
    (void)readOnly;
#endif
    return result;
}

} // namespace HIKARI::EDITOR
