#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/Authoring/HIKARI_SceneFolderAuthoring.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
void HierarchyPanel::DrawFolderContents(World& world, EditorSelection& selection,
                                        const std::vector<GameObject*>& objects, std::string_view search,
                                        const SceneDocument& document,
                                        const EDITOR::EditorOperationRegistry& operations,
                                        const std::function<void(GameObject*)>& drawObject,
                                        std::uint64_t documentRevision) {
#if defined(HIKARI_WITH_EDITOR)
    const bool changedDocument = folderDocumentRevision_ != documentRevision;
    if (changedDocument) {
        folderDocumentRevision_ = documentRevision;
        folderPopupRequested_ = false;
        folderMessage_.clear();
    }
    const auto parent = [](const std::string& path) {
        const auto slash = path.rfind('/');
        return slash == std::string::npos ? std::string{} : path.substr(0, slash);
    };
    const auto leaf = [](const std::string& path) { return path.substr(path.rfind('/') + 1); };
    const auto join = [](const std::string& a, const std::string& b) { return a.empty() ? b : a + "/" + b; };
    const auto execute = [&](const char* id, const nlohmann::json& parameters) {
        const auto result = operations.Execute(id, parameters);
        folderMessage_ = result.ok ? std::string{} : result.message;
        return result.ok;
    };
    const auto beginName = [&](std::string path, bool rename) {
        folderSource_ = std::move(path);
        folderRename_ = rename;
        folderNameBuffer_.fill('\0');
        if (rename) {
            const auto name = leaf(folderSource_);
            std::copy_n(name.data(), std::min(name.size(), folderNameBuffer_.size() - 1), folderNameBuffer_.data());
        }
        folderPopupRequested_ = true;
    };
    if (ImGui::Button(TEXT::LocalizeWindow("editor.newFolder", "新規フォルダー").c_str()))
        beginName({}, false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(TEXT::Localize("editor.folderHint").c_str());
    int expansion = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.expandAll", "すべて開く").c_str()))
        expansion = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.collapseAll", "すべて閉じる").c_str()))
        expansion = -1;
    if (!folderMessage_.empty())
        ImGui::TextWrapped("%s", folderMessage_.c_str());

    const auto folders = EDITOR::CollectSceneFolderPaths(document);
    std::unordered_map<std::uint64_t, std::string> membership;
    for (const auto& object : document.objects)
        membership[object.id.value] = object.editorFolder;
    std::map<std::string, std::vector<std::string>> children;
    std::map<std::string, std::vector<GameObject*>> members;
    std::map<std::string, std::size_t> counts;
    std::set<std::string> visible;
    for (const auto& folder : folders) {
        children[parent(folder)].push_back(folder);
        if (search.empty() || TEXT::ToLowerAsciiCopy(folder).find(search) != std::string::npos) {
            for (auto path = folder; !path.empty(); path = parent(path))
                visible.insert(path);
        }
    }
    for (auto* object : objects) {
        auto path = membership[object->GetDocumentId().value];
        if (!EDITOR::IsValidSceneFolderPath(path))
            path.clear();
        members[path].push_back(object);
        for (auto ancestor = path; !ancestor.empty(); ancestor = parent(ancestor)) {
            ++counts[ancestor];
            visible.insert(ancestor);
        }
    }
    const auto selectedIds = [&]() {
        nlohmann::json ids = nlohmann::json::array();
        for (const auto id : selection.GetSelectedObjectIds())
            ids.push_back(id.value);
        return ids;
    };
    const auto dropTarget = [&](const std::string& path) {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const auto* payload = ImGui::AcceptDragDropPayload("HIKARI_SCENE_OBJECT_IDS")) {
            if (payload->DataSize > 0 && payload->DataSize % sizeof(SceneObjectId) == 0) {
                nlohmann::json ids = nlohmann::json::array();
                const auto* data = static_cast<const SceneObjectId*>(payload->Data);
                for (std::size_t i = 0; i < static_cast<std::size_t>(payload->DataSize) / sizeof(SceneObjectId); ++i)
                    ids.push_back(data[i].value);
                execute("scene.folder.assign", {{"path", path}, {"objectIds", ids}});
            }
        }
        if (const auto* payload = ImGui::AcceptDragDropPayload("HIKARI_SCENE_FOLDER_PATH")) {
            if (payload->DataSize > 1) {
                const std::string source(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                execute("scene.folder.rename", {{"path", source}, {"newPath", join(path, leaf(source))}});
            }
        }
        ImGui::EndDragDropTarget();
    };
    const auto drawMembers = [&](const std::string& path) {
        const auto& list = members[path];
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(list.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                drawObject(list[i]);
    };
    std::function<void(const std::string&)> drawFolder;
    drawFolder = [&](const std::string& path) {
        if (!visible.contains(path))
            return;
        ImGui::PushID(path.c_str());
        if (!search.empty())
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else if (expansion != 0)
            ImGui::SetNextItemOpen(expansion > 0, ImGuiCond_Always);
        const bool open = ImGui::TreeNodeEx("folder", ImGuiTreeNodeFlags_SpanAvailWidth, "   %s  (%zu)",
                                            leaf(path).c_str(), counts[path]);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        EDITOR::DrawEditorGlyph(*ImGui::GetWindowDrawList(), EDITOR::EditorGlyph::Folder,
                                {min.x + 24.f, (min.y + max.y) * .5f}, 12.f,
                                ImGui::GetColorU32(EDITOR::GetEditorThemePalette().accent));
        dropTarget(path);
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("HIKARI_SCENE_FOLDER_PATH", path.c_str(), path.size() + 1);
            ImGui::TextUnformatted(path.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("folder-menu")) {
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.newChildFolder", "子フォルダーを作成").c_str()))
                beginName(path, false);
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.rename", "名前を変更").c_str()))
                beginName(path, true);
            if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.folderSelection", "選択中のオブジェクトを入れる").c_str(),
                                nullptr, false, selection.GetSelectedObjectCount() > 0))
                execute("scene.folder.assign", {{"path", path}, {"objectIds", selectedIds()}});
            if (ImGui::MenuItem(
                    TEXT::LocalizeWindow("editor.selectContents", "中のオブジェクトをすべて選択").c_str())) {
                bool first = true;
                for (const auto& object : world.GetObjects())
                    if (object) {
                        const auto& folder = membership[object->GetDocumentId().value];
                        if (folder == path || folder.starts_with(path + "/")) {
                            selection.SelectObject(world, object.get(),
                                                   first ? EditorObjectSelectionMode::Replace
                                                         : EditorObjectSelectionMode::Add);
                            first = false;
                        }
                    }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(
                    TEXT::LocalizeWindow("editor.dissolveFolder", "フォルダーを解散（中身は保持）").c_str()))
                execute("scene.folder.remove", {{"path", path}});
            ImGui::EndPopup();
        }
        if (open) {
            for (const auto& child : children[path])
                drawFolder(child);
            drawMembers(path);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    for (const auto& root : children[""])
        drawFolder(root);
    const bool ungroupedOpen =
        ImGui::TreeNodeEx("ungrouped", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                          TEXT::Localize("editor.unfiled").c_str(), members[""].size());
    dropTarget({});
    if (ungroupedOpen) {
        drawMembers({});
        ImGui::TreePop();
    }
    if (folderPopupRequested_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("editor.folderName", "フォルダー名").c_str());
        folderPopupRequested_ = false;
    }
    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("editor.folderName", "フォルダー名").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (changedDocument)
            ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted(folderRename_ ? TEXT::Localize("editor.folder.rename").c_str()
                                             : TEXT::Localize("editor.folder.create").c_str());
        ImGui::TextDisabled("%s", folderSource_.empty() ? "シーン直下" : folderSource_.c_str());
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText(TEXT::LocalizeWindow("ui.input.name", "名前").c_str(), folderNameBuffer_.data(),
                         folderNameBuffer_.size());
        if (!folderMessage_.empty())
            ImGui::TextWrapped("%s", folderMessage_.c_str());
        if (ImGui::Button(folderRename_ ? "変更" : TEXT::Localize("editor.ui.create.6e157c").c_str())) {
            const auto target = join(folderRename_ ? parent(folderSource_) : folderSource_, folderNameBuffer_.data());
            const bool done = folderRename_
                                  ? execute("scene.folder.rename", {{"path", folderSource_}, {"newPath", target}})
                                  : execute("scene.folder.create", {{"path", target}});
            if (done)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "キャンセル").c_str())) {
            folderMessage_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    (void)world;
    (void)selection;
    (void)objects;
    (void)search;
    (void)document;
    (void)operations;
    (void)drawObject;
    (void)documentRevision;
#endif
}
} // namespace HIKARI
