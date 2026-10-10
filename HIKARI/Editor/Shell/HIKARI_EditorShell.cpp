#include "Editor/Shell/HIKARI_EditorShell.h"
#include "Editor/Settings/HIKARI_EditorPreferences.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspacePresentation.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI::EDITOR {
void EditorShell::Draw(EditorContext& context, EditorWorkspaceHost& workspaces, EditorCommandRouter& commands,
                       const EditorShellContents& contents) {
    const auto& prefs = GetEditorPreferences();
    const auto text = [](const char* key) { return TEXT::Localize(key); };
    const float scale = ImGui::GetFontSize() / 16.0f;
    const auto& theme = GetEditorThemePalette();
    static constexpr const char* pageNames[]{"", "workspaces", "resources", "assistant", "tasks", "log", "settings"};
    if (!initialized_) {
        width_ = prefs.sidebarWidth;
        for (int i = 0; i < IM_ARRAYSIZE(pageNames); ++i)
            if (prefs.sidebarPage == pageNames[i])
                page_ = static_cast<Page>(i);
        lastAssistant_ = context.windows.runtime.showAiAssistant = page_ == Page::Assistant;
        lastSettings_ = context.windows.runtime.showEditorSettings = page_ == Page::Settings;
        lastResources_ = context.windows.resources.showAssetBrowser = page_ == Page::Resources;
        initialized_ = true;
    }
    const Page previousPage = page_;
    if ((context.windows.runtime.showAiAssistant && !lastAssistant_) ||
        (contents.assistantAttention && !lastAssistantAttention_))
        page_ = Page::Assistant;
    lastAssistantAttention_ = contents.assistantAttention;
    if (context.windows.runtime.showEditorSettings && !lastSettings_)
        page_ = Page::Settings;
    if (context.windows.resources.showAssetBrowser && !lastResources_)
        page_ = Page::Resources;
    auto* viewport = ImGui::GetMainViewport();
    const auto history = CORE::GetLogHistoryInfo();
    const float railWidth = (prefs.navigationLabels ? 142.0f : 48.0f) * scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6 * scale, 8 * scale});
    if (ImGui::BeginViewportSideBar("###Editor/Navigation", viewport, ImGuiDir_Left, railWidth,
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto entry = [&](Page page, EditorGlyph glyph, const char* key, bool busy = false) {
            const auto tone = page_ == page ? EditorButtonTone::Primary : EditorButtonTone::Quiet;
            const auto label = text(key);
            const bool clicked = prefs.navigationLabels
                                     ? IconTextButton(glyph, label.c_str(), key, tone,
                                                      {railWidth - 12 * scale, 34 * scale}, label.c_str())
                                     : IconButton(glyph, key, tone, {34 * scale, 34 * scale}, label.c_str());
            if (clicked)
                page_ = page_ == page ? Page::None : page;
            if (busy) {
                const auto corner = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    {corner.x - 5, corner.y - 5}, 3 * scale,
                    ImGui::GetColorU32(page == Page::Log ? theme.error : theme.accentHover));
            }
        };
        entry(Page::Workspaces, EditorGlyph::Grid, "shell.workspaces");
        entry(Page::Resources, EditorGlyph::Library, "shell.resources");
        ImGui::Separator();
        entry(Page::Assistant, EditorGlyph::Assistant, "shell.ai",
              contents.assistantBusy || contents.assistantAttention);
        entry(Page::Tasks, EditorGlyph::Ready, "shell.tasks", contents.tasksBusy);
        entry(Page::Log, EditorGlyph::Log, "shell.log", history.errors > readErrors_);
        ImGui::SetCursorPosY((std::max)(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 48 * scale));
        entry(Page::Settings, EditorGlyph::Settings, "settings.title");
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (page_ != Page::None) {
        const float maximum = (std::max)(140.0f, viewport->Size.x - railWidth - 180.0f * scale);
        const float width = (std::min)(width_ * scale, maximum);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12 * scale, 12 * scale});
        if (ImGui::BeginViewportSideBar("###Editor/Sidebar", viewport, ImGuiDir_Left, width,
                                        ImGuiWindowFlags_NoSavedSettings)) {
            const auto position = ImGui::GetWindowPos();
            const ImRect resize{{position.x + width - 5, position.y},
                                {position.x + width, position.y + ImGui::GetWindowHeight()}};
            const auto resizeId = ImGui::GetID("sidebarResize");
            bool hovered = false, held = false;
            ImGui::ItemAdd(resize, resizeId);
            ImGui::ButtonBehavior(resize, resizeId, &hovered, &held);
            if (hovered || held)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (held)
                width_ = std::clamp(width_ + ImGui::GetIO().MouseDelta.x / scale, 260.0f, 680.0f);
            if (resizing_ && !held) {
                auto next = prefs;
                next.sidebarWidth = width_;
                SaveEditorPreferences(next);
            }
            resizing_ = held;
            const char* names[]{"",          "shell.workspaces", "shell.resources", "shell.ai", "shell.tasks",
                                "shell.log", "settings.title"};
            ImGui::TextUnformatted(text(names[static_cast<int>(page_)]).c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 14 * scale);
            if (IconButton(EditorGlyph::Close, "closeSidebar", EditorButtonTone::Quiet, {24 * scale, 24 * scale},
                           text("shell.close").c_str()))
                page_ = Page::None;
            ImGui::Separator();
            switch (page_) {
            case Page::Workspaces:
                SearchField("workspaceSearch", text("shell.searchWorkspaces").c_str(), workspaceFilter_,
                            sizeof(workspaceFilter_));
                for (const auto& workspace : kEditorWorkspacePresentations) {
                    const auto label = text(workspace.nameKey);
                    if (workspaceFilter_[0] && label.find(workspaceFilter_) == std::string::npos)
                        continue;
                    if (IconTextButton(workspace.icon, label.c_str(), workspace.nameKey,
                                       workspaces.IsActive(workspace.id) ? EditorButtonTone::Primary
                                                                         : EditorButtonTone::Quiet,
                                       {-1, 36 * scale}))
                        workspaces.RequestOpen({workspace.id});
                }
                if (contents.projectWorkspaces)
                    contents.projectWorkspaces(workspaceFilter_);
                ImGui::Separator();
                if (IconTextButton(EditorGlyph::Refresh, text("shell.resetLayout").c_str(), "resetWorkspace"))
                    workspaces.RequestResetActiveLayout();
                break;
            case Page::Resources:
                if (contents.resources)
                    contents.resources();
                break;
            case Page::Assistant:
                if (contents.assistant)
                    contents.assistant();
                break;
            case Page::Tasks:
                if (contents.tasks)
                    contents.tasks();
                break;
            case Page::Settings:
                if (contents.settings)
                    contents.settings();
                break;
            case Page::Log:
                readErrors_ = history.errors;
                if (IconTextButton(EditorGlyph::Reveal, text("log.detach").c_str(), "detachLog")) {
                    detachedLog_ = true;
                    page_ = Page::None;
                } else
                    log_.DrawContents();
                break;
            default: break;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    if (detachedLog_) {
        ImGui::SetNextWindowSize({720, 320}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(TEXT::LocalizeWindow("shell.log", "Editor/Log").c_str(), &detachedLog_)) {
            readErrors_ = history.errors;
            if (IconTextButton(EditorGlyph::CollapseRight, text("log.attach").c_str(), "attachLog")) {
                detachedLog_ = false;
                page_ = Page::Log;
            } else
                log_.DrawContents();
        }
        ImGui::End();
    }
    lastAssistant_ = context.windows.runtime.showAiAssistant = page_ == Page::Assistant;
    lastSettings_ = context.windows.runtime.showEditorSettings = page_ == Page::Settings;
    lastResources_ = context.windows.resources.showAssetBrowser = page_ == Page::Resources;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8 * scale, 2 * scale});
    if (ImGui::BeginViewportSideBar("###Editor/Status", viewport, ImGuiDir_Down, 30 * scale,
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
        for (const auto command : {EditorCommandId::SaveDocument, EditorCommandId::Undo, EditorCommandId::Redo}) {
            const auto glyph = command == EditorCommandId::SaveDocument ? EditorGlyph::Save
                               : command == EditorCommandId::Undo       ? EditorGlyph::Undo
                                                                        : EditorGlyph::Redo;
            const auto* binding = commands.Find(command);
            ImGui::PushID(static_cast<int>(command));
            ImGui::BeginDisabled(!commands.CanExecute(command));
            if (IconButton(glyph, "documentCommand", EditorButtonTone::Quiet, {24 * scale, 24 * scale},
                           binding ? binding->label.c_str() : ""))
                commands.Execute(command);
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s",
                            text(contents.tasksBusy || contents.assistantBusy ? "shell.busy" : "shell.ready").c_str());
        ImGui::SameLine();
        const auto count = text("log.warning") + " " + std::to_string(history.warnings) + "  " + text("log.error") +
                           " " + std::to_string(history.errors);
        if (ImGui::SmallButton((count + "###openLogStatus").c_str()))
            page_ = Page::Log;
        if (!contents.unsavedSummary.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s",
                               (text("shell.unsavedList") + contents.unsavedSummary).c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (contents.documents)
        contents.documents();
    // サイドバーの領域を同じフレームの DockBuilder に公開する。
    // 既定の WorkRect は前フレーム由来なので、初回配置で中央ビューが縮まってしまう。
    const auto workRect = static_cast<ImGuiViewportP*>(viewport)->GetBuildWorkRect();
    viewport->WorkPos = workRect.Min;
    viewport->WorkSize = workRect.GetSize();
    if (page_ != previousPage) {
        auto next = GetEditorPreferences();
        next.sidebarPage = pageNames[static_cast<int>(page_)];
        SaveEditorPreferences(next);
    }
}
} // namespace HIKARI::EDITOR
