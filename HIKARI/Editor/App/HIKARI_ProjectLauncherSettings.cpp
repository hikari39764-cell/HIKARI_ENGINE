#include "HIKARI_ProjectLauncherApp.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Settings/HIKARI_EditorPreferenceControls.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
void ProjectLauncherApp::DrawSettings() {
    PanelTitle(TEXT::Localize("settings.title").c_str(), TEXT::Localize("launcher.sharedPreferences").c_str());
    auto preferences = GetEditorPreferences();
    bool changed = false;
    for (const char* key :
         {"settings.language", "shell.scale", "shell.highContrast", "shell.compact", "shell.tooltips"}) {
        changed |= DrawEditorPreferenceControl(key, preferences);
        ImGui::Spacing();
    }
    ImGui::SeparatorText(TEXT::Localize("launcher.defaultLocation").c_str());
    ImGui::TextWrapped("%s", projectLocation_.data());
    if (IconTextButton(EditorGlyph::Folder, TEXT::Localize("editor.ui.browse.2f3b5c").c_str(), "defaultParent")) {
        BrowseForCreationLocation();
        preferences.projectParentDirectory = projectLocation_.data();
        changed = true;
    }
    if (changed)
        SaveEditorPreferences(preferences);
    if (!EditorPreferencesError().empty())
        ImGui::TextWrapped("%s", EditorPreferencesError().c_str());
    ImGui::SeparatorText(TEXT::Localize("launcher.engineLocation").c_str());
    ImGui::TextWrapped("%s", TEXT::PathToUtf8(runtimeRoot_).c_str());
    if (IconTextButton(EditorGlyph::Folder, TEXT::Localize("launcher.showFolder").c_str(), "engineFolder"))
        SHELL::OpenFolderInExplorer(runtimeRoot_);
}
void ProjectLauncherApp::DrawLogs() {
    if (!ImGui::BeginTabBar("logs"))
        return;
    if (ImGui::BeginTabItem(TEXT::Localize("launcher.projectStartup").c_str())) {
        if (IconButton(EditorGlyph::Duplicate, "copyLog", EditorButtonTone::Quiet, {28, 28},
                       TEXT::Localize("ui.materialGraph.copy").c_str()))
            ImGui::SetClipboardText(openWorkflow_.GetLogTail().c_str());
        NextToolbarItem();
        if (IconButton(EditorGlyph::Folder, "logFolder", EditorButtonTone::Quiet, {28, 28},
                       TEXT::Localize("launcher.showFolder").c_str()) &&
            !openWorkflow_.GetBuildLogPath().empty())
            SHELL::OpenFolderInExplorer(openWorkflow_.GetBuildLogPath().parent_path());
        ImGui::BeginChild("buildLog", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
        const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 24;
        ImGui::TextUnformatted(openWorkflow_.GetLogTail().c_str());
        if (follow)
            ImGui::SetScrollHereY(1);
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(TEXT::Localize("shell.diagnostics").c_str())) {
        logPanel_.DrawContents();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace HIKARI::EDITOR
