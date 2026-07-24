#include "HIKARI_HierarchyPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {

        bool MatchesSearch(
            const GameObject& object,
            std::string_view lowerSearch) {
            if (lowerSearch.empty()) {
                return true;
            }
            return TEXT::ToLowerAsciiCopy(object.GetName()).find(lowerSearch) !=
                std::string::npos;
        }
    }

    void HierarchyPanel::Draw(World& world, EditorSelection& selection) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Scene Hierarchy")) {
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

    void HierarchyPanel::DrawContents(World& world, EditorSelection& selection) {
        DrawContents(world, selection, {});
    }

    void HierarchyPanel::DrawContents(
        World& world,
        EditorSelection& selection,
        const std::function<void(GameObject&)>&
            drawObjectContextMenu,
        const std::function<bool(const GameObject&)>&
            isObjectLocked) {
#if defined(HIKARI_WITH_EDITOR)
        const auto& objects = world.GetObjects();
        EDITOR::SearchField(
            "Hierarchy",
            "Search scene objects...",
            searchBuffer_.data(),
            searchBuffer_.size());

        std::vector<GameObject*> filteredObjects{};
        filteredObjects.reserve(objects.size());
        const std::string lowerSearch = TEXT::ToLowerAsciiCopy(searchBuffer_.data());
        for (const auto& object : objects) {
            if (object != nullptr &&
                MatchesSearch(*object, lowerSearch)) {
                filteredObjects.push_back(object.get());
            }
        }

        ImGui::TextDisabled(
            searchBuffer_[0] == '\0'
                ? "%d objects"
                : "%d of %d objects",
            static_cast<int>(filteredObjects.size()),
            static_cast<int>(objects.size()));
        if (selection.GetSelectedObjectCount() > 1u) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "| %d selected",
                static_cast<int>(
                    selection.GetSelectedObjectCount()));
        }

        if (objects.empty()) {
            EDITOR::EmptyState(
                "No scene objects",
                "Use + or right-click to create one.");
            return;
        }
        if (filteredObjects.empty()) {
            EDITOR::EmptyState(
                "No matching objects",
                "Try another name or clear the search.");
            return;
        }

        const EDITOR::EditorThemeMetrics& metrics =
            EDITOR::GetEditorThemeMetrics();
        ImGuiListClipper clipper{};
        clipper.Begin(static_cast<int>(filteredObjects.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart;
                index < clipper.DisplayEnd;
                ++index) {
                GameObject* objectPtr = filteredObjects[
                    static_cast<std::size_t>(index)];
                ImGui::PushID(objectPtr);
                const bool isSelected =
                    selection.IsObjectSelected(
                        objectPtr->GetDocumentId());
                const bool editorLocked =
                    isObjectLocked &&
                    isObjectLocked(*objectPtr);
                const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(16.0f, metrics.rowHeight));
                EDITOR::DrawEditorGlyph(
                    *ImGui::GetWindowDrawList(),
                    EDITOR::EditorGlyph::Object,
                    ImVec2(
                        iconMin.x + 8.0f,
                        iconMin.y + metrics.rowHeight * 0.5f),
                    13.0f,
                    ImGui::GetColorU32(
                        EDITOR::GetEditorThemePalette().accent));
                ImGui::SameLine();
                if (ImGui::Selectable(
                        objectPtr->GetName().c_str(),
                        isSelected,
                        ImGuiSelectableFlags_None,
                        ImVec2(0.0f, metrics.rowHeight))) {
                    const ImGuiIO& io = ImGui::GetIO();
                    const EditorObjectSelectionMode mode = io.KeyCtrl
                        ? EditorObjectSelectionMode::Toggle
                        : (io.KeyShift
                            ? EditorObjectSelectionMode::Add
                            : EditorObjectSelectionMode::Replace);
                    selection.SelectObject(world, objectPtr, mode);
                }
                if (editorLocked) {
                    const ImVec2 rowMin = ImGui::GetItemRectMin();
                    const ImVec2 rowMax = ImGui::GetItemRectMax();
                    EDITOR::DrawEditorGlyph(
                        *ImGui::GetWindowDrawList(),
                        EDITOR::EditorGlyph::Lock,
                        ImVec2(
                            rowMax.x - 10.0f,
                            (rowMin.y + rowMax.y) * 0.5f),
                        12.0f,
                        ImGui::GetColorU32(
                            EDITOR::GetEditorThemePalette().
                                textMuted));
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (!selection.IsObjectSelected(
                            objectPtr->GetDocumentId())) {
                        selection.SelectObject(world, objectPtr);
                    }
                }
                if (drawObjectContextMenu &&
                    ImGui::BeginPopupContextItem("ObjectContextMenu")) {
                    drawObjectContextMenu(*objectPtr);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
#else
        (void)world;
        (void)selection;
        (void)drawObjectContextMenu;
#endif
    }

} // namespace HIKARI
