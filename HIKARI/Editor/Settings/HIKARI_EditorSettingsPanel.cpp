#include "HIKARI_EditorPreferenceControls.h"
#include "Editor/Settings/HIKARI_EditorSettingsPanel.h"
#include "Editor/Settings/HIKARI_EditorPreferences.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
void EditorSettingsPanel::DrawContents(EditorContext& context, const std::function<void()>& bridgeActions) {
    auto preferences = GetEditorPreferences();
    bool changed = false;
    const auto tr = [](const char* key) { return TEXT::Localize(key); };
    SearchField("settingsSearch", tr("shell.searchSettings").c_str(), search_, sizeof(search_));
    const auto matches = [&](const char* key) {
        return !search_[0] ||
               TEXT::ToLowerAsciiCopy(tr(key)).find(TEXT::ToLowerAsciiCopy(search_)) != std::string::npos;
    };
    const char* categories[]{"shell.general", "shell.appearance", "shell.controls", "shell.ai", "shell.diagnostics"};
    ImGui::BeginChild("settingsCategories", {0, ImGui::GetFrameHeightWithSpacing() * 3}, false);
    for (int i = 0; i < 5; ++i) {
        if (i && ImGui::GetItemRectMax().x + ImGui::CalcTextSize(tr(categories[i]).c_str()).x + 32 <
                     ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine();
        const auto label = tr(categories[i]) + "##settingsCategory" + std::to_string(i);
        if (ToggleButton(label.c_str(), categories[i], category_ == i))
            category_ = i;
    }
    ImGui::EndChild();
    ImGui::BeginChild("settingsBody");
    if (category_ == 0 || search_[0]) {
        for (const char* key : {"settings.language", "shell.tooltips"})
            if (matches(key))
                changed |= DrawEditorPreferenceControl(key, preferences);
    }
    if (category_ == 1 || search_[0]) {
        for (const char* key : {"shell.scale", "shell.compact", "shell.highContrast", "shell.navigationLabels"})
            if (matches(key))
                changed |= DrawEditorPreferenceControl(key, preferences);
    }
    if (category_ == 2 || search_[0]) {
        if (matches("shell.shortcuts")) {
            ImGui::SeparatorText(tr("shell.shortcuts").c_str());
            ImGui::TextWrapped("%s", tr("shell.shortcutHelp").c_str());
        }
    }
    if (category_ == 3) {
        ImGui::SeparatorText(tr("settings.aiBridge").c_str());
        ImGui::Checkbox(tr("settings.aiBridge.enable").c_str(), &context.aiIntegration.localBridgeEnabled);
        ImGui::TextWrapped("%s", tr("settings.aiBridge.warning").c_str());
        if (!context.aiIntegration.localBridgeStatus.empty())
            ImGui::TextWrapped("%s", context.aiIntegration.localBridgeStatus.c_str());
        if (bridgeActions)
            bridgeActions();
    }
    if (category_ == 4 || search_[0]) {
        if (matches("shell.logCapacity")) {
            ImGui::TextUnformatted(tr("shell.logCapacity").c_str());
            if (!logCapacity_)
                logCapacity_ = preferences.logCapacity;
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##logCapacity", &logCapacity_, 500);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                preferences.logCapacity = logCapacity_;
                changed = true;
            }
        }
        if (IconTextButton(EditorGlyph::Folder, tr("shell.settingsFolder").c_str(), "openSettings"))
            SHELL::OpenFolderInExplorer(EditorUserSettingsDirectory());
    }
    if (changed)
        SaveEditorPreferences(preferences);
    if (!EditorPreferencesError().empty()) {
        ImGui::TextWrapped("%s", tr("shell.settingsSaveFailed").c_str());
        ImGui::TextWrapped("%s", EditorPreferencesError().c_str());
    }
    ImGui::EndChild();
}
} // namespace HIKARI::EDITOR
