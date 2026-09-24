#include "HIKARI_ProjectLauncherApp.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Platform/HIKARI_EditorFileDialog.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Render2D/HIKARI_DxTexture.h"
#include <algorithm>
#include "imgui.h"

namespace HIKARI::EDITOR {
void ProjectLauncherApp::DrawRecentProjectsPanel() {
    PanelTitle(TEXT::LocalizeCStr("launcher.projects"));
    NextToolbarItem(120);
    ImGui::BeginDisabled(openWorkflow_.IsBusy());
    if (IconTextButton(EditorGlyph::Folder, TEXT::LocalizeCStr("editor.ui.open.project.68e7c2"), "browse",
                       EditorButtonTone::Primary))
        BrowseForProject();
    ImGui::EndDisabled();
    ImGui::Spacing();
    SearchField("projectsSearch", TEXT::LocalizeCStr("launcher.search"), search_.data(), search_.size());
    if (IconToggleButton(EditorGlyph::Keyframe, "pinnedOnly", pinnedOnly_, {28, 28},
                         TEXT::LocalizeCStr("launcher.pinnedOnly")))
        pinnedOnly_ = !pinnedOnly_;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", recentProjects_.GetEntries().size());
    ImGui::Separator();
    const auto filter = TEXT::ToLowerAsciiCopy(search_.data());
    const auto entries = recentProjects_.GetEntries();
    size_t visible = 0;
    const int columns = ImGui::GetContentRegionAvail().x >= 1000.0f ? 2 : 1;
    const bool table = ImGui::BeginTable("projectCards", columns, ImGuiTableFlags_SizingStretchSame);
    if (!table)
        return;
    for (const auto& entry : entries) {
        const auto path = TEXT::PathToUtf8(entry.root);
        if (pinnedOnly_ && !entry.pinned)
            continue;
        if (!filter.empty() && TEXT::ToLowerAsciiCopy(entry.name + " " + path).find(filter) == std::string::npos)
            continue;
        ++visible;
        ImGui::TableNextColumn();
        if (!ImGui::IsRectVisible({ImGui::GetContentRegionAvail().x, 112})) {
            ImGui::Dummy({0, 112});
            continue;
        }
        ImGui::PushID(path.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorThemePalette().raised);
        ImGui::BeginChild("project", {0, 112}, true, ImGuiWindowFlags_NoScrollbar);
        const auto origin = ImGui::GetCursorScreenPos();
        constexpr float side = 64;
        auto& thumbnail = thumbnails_[path];
        const auto imagePath = entry.thumbnail.empty()
                                   ? TEXT::PathToUtf8(entry.root / "Library/Editor/ProjectThumbnail.png")
                                   : entry.thumbnail;
        if (thumbnail.path != imagePath) {
            if (thumbnail.handle >= 0)
                DXTEX::DxTextureManager::ReleaseTextureDeferred(thumbnail.handle);
            thumbnail = {-1, imagePath};
            std::error_code ec;
            if (std::filesystem::is_regular_file(TEXT::PathFromUtf8(imagePath), ec))
                thumbnail.handle = DXTEX::DxTextureManager::LoadTextureLinear("project/" + imagePath, imagePath);
        }
        const auto srv = thumbnail.handle >= 0 ? DXTEX::DxTextureManager::GetSrvGpuHandle(thumbnail.handle)
                                               : D3D12_GPU_DESCRIPTOR_HANDLE{};
        if (srv.ptr) {
            uint32_t width = 0, height = 0;
            DXTEX::DxTextureManager::GetTextureSize(thumbnail.handle, width, height);
            const float aspect = height ? float(width) / height : 1;
            const ImVec2 size = aspect >= 1 ? ImVec2{side, side / aspect} : ImVec2{side * aspect, side};
            ImGui::SetCursorScreenPos({origin.x + (side - size.x) / 2, origin.y + (side - size.y) / 2});
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)), size);
        } else {
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + side, origin.y + side},
                                ImGui::GetColorU32(GetEditorThemePalette().panel), 8);
            DrawEditorGlyph(*draw, EditorGlyph::Scene, {origin.x + side / 2, origin.y + side / 2}, 28,
                            ImGui::GetColorU32(GetEditorThemePalette().accent));
        }
        ImGui::SetCursorScreenPos({origin.x + side + 12, origin.y});
        ImGui::BeginGroup();
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGui::GetWindowDrawList()->PushClipRect({origin.x + side + 12, origin.y}, {right, origin.y + 44}, true);
        ImGui::TextUnformatted(entry.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.name.c_str());
        ImGui::TextDisabled("%s", path.c_str());
        ImGui::GetWindowDrawList()->PopClipRect();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", path.c_str());
        ImGui::BeginDisabled(openWorkflow_.IsBusy());
        if (IconTextButton(entry.available ? EditorGlyph::Play : EditorGlyph::Folder,
                           TEXT::LocalizeCStr(entry.available ? "editor.ui.open.cf9b77" : "launcher.locate"), "open",
                           EditorButtonTone::Primary, {0, 28})) {
            if (entry.available)
                OpenProject(entry.root);
            else if (auto replacement = FILE_DIALOG::PickFolder(entry.root.parent_path()))
                recentProjects_.Relocate(entry.root, *replacement, &interactionMessage_);
        }
        ImGui::EndDisabled();
        NextToolbarItem();
        if (IconToggleButton(EditorGlyph::Keyframe, "pin", entry.pinned, {28, 28},
                             TEXT::LocalizeCStr(entry.pinned ? "launcher.unpin" : "launcher.pin")))
            recentProjects_.SetPinned(entry.root, !entry.pinned, &interactionMessage_);
        NextToolbarItem();
        if (IconButton(EditorGlyph::More, "more", EditorButtonTone::Quiet, {28, 28},
                       TEXT::LocalizeCStr("launcher.more")))
            ImGui::OpenPopup("projectMenu");
        if (ImGui::BeginPopup("projectMenu")) {
            if (ImGui::MenuItem(TEXT::LocalizeCStr("launcher.showFolder")))
                SHELL::OpenFolderInExplorer(entry.root);
            if (ImGui::MenuItem(TEXT::LocalizeCStr("launcher.chooseImage"))) {
                if (auto selected = FILE_DIALOG::PickFile(entry.root, L"Images", L"*.png;*.jpg;*.jpeg"))
                    recentProjects_.SetThumbnail(entry.root, TEXT::PathToUtf8(*selected), &interactionMessage_);
            }
            if (ImGui::MenuItem(TEXT::LocalizeCStr("launcher.locate"))) {
                if (auto selected = FILE_DIALOG::PickFolder(entry.root.parent_path()))
                    recentProjects_.Relocate(entry.root, *selected, &interactionMessage_);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TEXT::LocalizeCStr("editor.ui.remove.from.recent.projects.f1a467")))
                recentProjects_.Remove(entry.root, &interactionMessage_);
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::EndTable();
    if (!visible)
        EmptyState(TEXT::LocalizeCStr("launcher.noMatches"), TEXT::LocalizeCStr("launcher.emptyHint"));
}

void ProjectLauncherApp::DrawCreateProjectPanel() {
    PanelTitle(TEXT::LocalizeCStr("launcher.newProject"));
    ImGui::TextDisabled("%s", TEXT::LocalizeCStr("launcher.blankTemplate"));
    ImGui::Spacing();
    ImGui::TextUnformatted(TEXT::LocalizeCStr("editor.ui.project.name.ab9773"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("name", "MyGame", projectName_.data(), projectName_.size());
    ImGui::TextUnformatted(TEXT::LocalizeCStr("editor.ui.location.d219c6"));
    ImGui::SetNextItemWidth(-42);
    ImGui::InputText("##location", projectLocation_.data(), projectLocation_.size());
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Folder, "location", EditorButtonTone::Quiet, {30, 0},
                   TEXT::LocalizeCStr("editor.ui.browse.2f3b5c")))
        BrowseForCreationLocation();
    std::filesystem::path target;
    std::string validation;
    const bool valid =
        ValidateProjectCreation({TEXT::PathFromUtf8(projectLocation_.data()), projectName_.data()}, target, validation);
    if (!target.empty())
        ImGui::TextWrapped("%s", TEXT::PathToUtf8(target).c_str());
    if (!valid && projectName_[0])
        ImGui::TextWrapped("%s", validation.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(!valid || openWorkflow_.IsBusy());
    if (IconTextButton(EditorGlyph::Add, TEXT::LocalizeCStr("editor.ui.create.and.open.f53899"), "create",
                       EditorButtonTone::Primary, {0, 36}))
        CreateAndOpenProject();
    ImGui::EndDisabled();
}
} // namespace HIKARI::EDITOR
