#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
bool ContainsInsensitive(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](unsigned char left, unsigned char right) {
                           return std::tolower(left) == std::tolower(right);
                       }) != text.end();
}

bool MatchesCatalogSearch(std::string_view name, std::string_view id, const char* query) {
    const std::string_view filter = query != nullptr ? std::string_view(query) : std::string_view{};
    return ContainsInsensitive(name, filter) || ContainsInsensitive(id, filter);
}
#endif
} // namespace

void RuntimeBindingWorkspaceController::DrawCatalogWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.catalog.4a88d2", "RuntimeBinding/Catalog").c_str())) {
        ImGui::End();
        return;
    }

    PanelTitle(TEXT::Localize("editor.ui.variables.events.8ed3ba").c_str(),
               TEXT::Localize("editor.ui.project.owned.ids.available.to.components.and.tools.26f74f").c_str());
    if (ToggleButton(TEXT::Localize("ui.assetBrowser.filterAll").c_str(), "RuntimeBindingFilterAll",
                     catalogFilter_ == CatalogFilter::All, ImVec2(58.0f, 26.0f))) {
        catalogFilter_ = CatalogFilter::All;
    }
    NextToolbarItem(90);
    if (ToggleButton(TEXT::Localize("editor.ui.variables.ac018d").c_str(), "RuntimeBindingFilterVariables",
                     catalogFilter_ == CatalogFilter::Variables, ImVec2(84.0f, 26.0f))) {
        catalogFilter_ = CatalogFilter::Variables;
    }
    NextToolbarItem(90);
    if (ToggleButton(TEXT::Localize("editor.ui.events.c5497b").c_str(), "RuntimeBindingFilterEvents",
                     catalogFilter_ == CatalogFilter::Events, ImVec2(72.0f, 26.0f))) {
        catalogFilter_ = CatalogFilter::Events;
    }
    ImGui::Separator();

    SearchField("RuntimeBindingSearch", TEXT::Localize("editor.ui.search.name.or.stable.id.874d5c").c_str(),
                searchBuffer_.data(), searchBuffer_.size());
    const BINDING::Catalog& catalog = document_.Catalog();
    size_t visibleCount = 0u;
    std::string deleteRequestedId{};
    if (catalogFilter_ != CatalogFilter::Events) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.variables.ac018d").c_str());
        for (const BINDING::VariableDefinition& variable : catalog.variables) {
            if (!MatchesCatalogSearch(variable.name, variable.id, searchBuffer_.data())) {
                continue;
            }
            ++visibleCount;
            ImGui::PushID(variable.id.c_str());
            const bool selected = selectionKind_ == SelectionKind::Variable && selectedItemId_ == variable.id;
            if (ImGui::Selectable((variable.name + "##row").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 34.0f))) {
                SelectItem(variable.id);
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeightWithSpacing());
            ImGui::TextDisabled("%s", std::string(BINDING::ToString(variable.defaultValue.type)).c_str());
            if (ImGui::BeginPopupContextItem("CatalogItemContext")) {
                if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.common.delete", "Delete").c_str())) {
                    deleteRequestedId = variable.id;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    if (catalogFilter_ != CatalogFilter::Variables) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.events.c5497b").c_str());
        for (const BINDING::EventDefinition& event : catalog.events) {
            if (!MatchesCatalogSearch(event.name, event.id, searchBuffer_.data())) {
                continue;
            }
            ++visibleCount;
            ImGui::PushID(event.id.c_str());
            const bool selected = selectionKind_ == SelectionKind::Event && selectedItemId_ == event.id;
            if (ImGui::Selectable((event.name + "##row").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, 34.0f))) {
                SelectItem(event.id);
            }
            const std::string eventType =
                event.hasPayload ? "Event<" + std::string(BINDING::ToString(event.payloadType)) + ">" : "Event";
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeightWithSpacing());
            ImGui::TextDisabled("%s", eventType.c_str());
            if (ImGui::BeginPopupContextItem("CatalogItemContext")) {
                if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.common.delete", "Delete").c_str())) {
                    deleteRequestedId = event.id;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    if (visibleCount == 0u) {
        EmptyState(searchBuffer_[0] == '\0' ? TEXT::Localize("editor.ui.no.bindings.yet.3980b1").c_str()
                                            : TEXT::Localize("editor.ui.no.matching.bindings.10a205").c_str(),
                   searchBuffer_[0] == '\0'
                       ? TEXT::Localize("editor.ui.use.add.in.the.toolbar.to.create.a.variable.or.event.1cda5c").c_str()
                       : TEXT::Localize("editor.ui.try.another.name.or.stable.id.a9c8a0").c_str());
    }
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete,
                              ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
        deleteRequestedId = selectedItemId_;
    ImGui::End();

    if (!deleteRequestedId.empty()) {
        const RuntimeBindingRemovalResult removal = RemoveItem(scene, deleteRequestedId, false);
        statusMessage_ = removal.message;
        if (removal.confirmationRequired) {
            pendingDeleteItemId_ = deleteRequestedId;
            pendingDeleteReferences_ = removal.references;
            requestDeleteDialog_ = true;
        }
    }
#else
    (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
