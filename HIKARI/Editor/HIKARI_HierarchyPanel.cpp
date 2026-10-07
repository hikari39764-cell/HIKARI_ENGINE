#include "HIKARI_HierarchyPanel.h"
#include "Editor/Authoring/HIKARI_SceneFolderAuthoring.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/HIKARI_World.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
namespace {

bool MatchesSearch(const GameObject& object, std::string_view lowerSearch) {
    if (lowerSearch.empty()) {
        return true;
    }
    return TEXT::ToLowerAsciiCopy(object.GetName()).find(lowerSearch) != std::string::npos;
}

EDITOR::EditorGlyph ResolveObjectGlyph(const GameObject& object) {
    if (object.GetComponent<CameraComponent>() != nullptr) {
        return EDITOR::EditorGlyph::Camera;
    }
    if (object.GetComponent<LightComponent>() != nullptr) {
        return EDITOR::EditorGlyph::Light;
    }
    if (object.GetComponent<ReflectionProbeComponent>() != nullptr) {
        return EDITOR::EditorGlyph::ReflectionProbe;
    }
    if (object.GetComponent<IrradianceVolumeComponent>() != nullptr) {
        return EDITOR::EditorGlyph::IrradianceVolume;
    }
    return EDITOR::EditorGlyph::Object;
}
} // namespace

void HierarchyPanel::Draw(World& world, EditorSelection& selection) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::Localize("menu.windows.sceneHierarchy").c_str())) {
        ImGui::End();
        return;
    }

    DrawContents(world, selection);

    ImGui::End();
#else
    (void)world;
    (void)selection;
#endif
}

void HierarchyPanel::DrawContents(World& world, EditorSelection& selection) { DrawContents(world, selection, {}); }

void HierarchyPanel::DrawContents(World& world, EditorSelection& selection,
                                  const std::function<void(GameObject&)>& drawObjectContextMenu,
                                  const std::function<bool(const GameObject&)>& isObjectLocked,
                                  const SceneDocument* document, const EDITOR::EditorOperationRegistry* operations,
                                  std::uint64_t documentRevision) {
#if defined(HIKARI_WITH_EDITOR)
    const auto& objects = world.GetObjects();
    EDITOR::SearchField(TEXT::Localize("ui.hierarchy.search").c_str(),
                        TEXT::Localize("ui.hierarchy.searchPlaceholder").c_str(), searchBuffer_.data(),
                        searchBuffer_.size());

    std::vector<GameObject*> filteredObjects{};
    filteredObjects.reserve(objects.size());
    const std::string lowerSearch = TEXT::ToLowerAsciiCopy(searchBuffer_.data());
    std::unordered_map<std::uint64_t, std::string> objectFolders;
    if (document && !lowerSearch.empty()) {
        objectFolders.reserve(document->objects.size());
        for (const auto& item : document->objects)
            objectFolders.emplace(item.id.value, TEXT::ToLowerAsciiCopy(item.editorFolder));
    }
    for (const auto& object : objects) {
        if (object == nullptr)
            continue;
        const auto folder = objectFolders.find(object->GetDocumentId().value);
        if (MatchesSearch(*object, lowerSearch) ||
            (folder != objectFolders.end() && folder->second.find(lowerSearch) != std::string::npos)) {
            filteredObjects.push_back(object.get());
        }
    }

    ImGui::TextDisabled(searchBuffer_[0] == '\0' ? TEXT::Localize("ui.hierarchy.objects").c_str()
                                                 : TEXT::Localize("ui.hierarchy.objectsFiltered").c_str(),
                        static_cast<int>(filteredObjects.size()), static_cast<int>(objects.size()));
    if (selection.GetSelectedObjectCount() > 1u) {
        ImGui::SameLine();
        ImGui::TextDisabled(TEXT::Localize("ui.hierarchy.selected").c_str(),
                            static_cast<int>(selection.GetSelectedObjectCount()));
    }

    const EDITOR::EditorThemeMetrics& metrics = EDITOR::GetEditorThemeMetrics();
    const auto drawObject = [&](GameObject* objectPtr) {
        ImGui::PushID(objectPtr);
        const bool isSelected = selection.IsObjectSelected(objectPtr->GetDocumentId());
        const bool editorLocked = isObjectLocked && isObjectLocked(*objectPtr);
        const ImVec2 iconMin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(16.0f, metrics.rowHeight));
        EDITOR::DrawEditorGlyph(*ImGui::GetWindowDrawList(), ResolveObjectGlyph(*objectPtr),
                                ImVec2(iconMin.x + 8.0f, iconMin.y + metrics.rowHeight * 0.5f), 13.0f,
                                ImGui::GetColorU32(EDITOR::GetEditorThemePalette().accent));
        ImGui::SameLine();
        if (ImGui::Selectable(objectPtr->GetName().c_str(), isSelected, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, metrics.rowHeight))) {
            const ImGuiIO& io = ImGui::GetIO();
            const EditorObjectSelectionMode mode =
                io.KeyCtrl ? EditorObjectSelectionMode::Toggle
                           : (io.KeyShift ? EditorObjectSelectionMode::Add : EditorObjectSelectionMode::Replace);
            selection.SelectObject(world, objectPtr, mode);
        }
        if (editorLocked) {
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            EDITOR::DrawEditorGlyph(*ImGui::GetWindowDrawList(), EDITOR::EditorGlyph::Lock,
                                    ImVec2(rowMax.x - 10.0f, (rowMin.y + rowMax.y) * 0.5f), 12.0f,
                                    ImGui::GetColorU32(EDITOR::GetEditorThemePalette().textMuted));
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!selection.IsObjectSelected(objectPtr->GetDocumentId())) {
                selection.SelectObject(world, objectPtr);
            }
        }
        if (document && operations && ImGui::BeginDragDropSource()) {
            if (!selection.IsObjectSelected(objectPtr->GetDocumentId()))
                selection.SelectObject(world, objectPtr);
            const auto& ids = selection.GetSelectedObjectIds();
            ImGui::SetDragDropPayload("HIKARI_SCENE_OBJECT_IDS", ids.data(), ids.size() * sizeof(SceneObjectId));
            ImGui::Text(TEXT::Localize("editor.objectCount").c_str(), ids.size());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("ObjectContextMenu")) {
            if (drawObjectContextMenu)
                drawObjectContextMenu(*objectPtr);
            if (document && operations &&
                ImGui::BeginMenu(TEXT::LocalizeWindow("editor.moveToFolder", "フォルダーへ移動").c_str())) {
                auto folders = EDITOR::CollectSceneFolderPaths(*document);
                folders.insert(folders.begin(), "");
                for (const auto& folder : folders)
                    if (ImGui::MenuItem(folder.empty() ? TEXT::Localize("editor.uncategorized").c_str()
                                                       : folder.c_str())) {
                        nlohmann::json ids = nlohmann::json::array();
                        for (const auto id : selection.GetSelectedObjectIds())
                            ids.push_back(id.value);
                        const auto result =
                            operations->Execute("scene.folder.assign", {{"path", folder}, {"objectIds", ids}});
                        folderMessage_ = result.ok ? std::string{} : result.message;
                    }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    if (document && operations) {
        DrawFolderContents(world, selection, filteredObjects, lowerSearch, *document, *operations, drawObject,
                           documentRevision);
    } else {
        ImGuiListClipper clipper{};
        clipper.Begin(static_cast<int>(filteredObjects.size()));
        while (clipper.Step())
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                drawObject(filteredObjects[index]);
    }
#else
    (void)world;
    (void)selection;
    (void)drawObjectContextMenu;
#endif
}

} // namespace HIKARI
