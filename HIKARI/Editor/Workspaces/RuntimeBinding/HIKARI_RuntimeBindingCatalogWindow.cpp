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
        bool ContainsInsensitive(
            std::string_view text,
            std::string_view query) {

            if (query.empty()) {
                return true;
            }
            return std::search(
                text.begin(), text.end(),
                query.begin(), query.end(),
                [](unsigned char left, unsigned char right) {
                    return std::tolower(left) == std::tolower(right);
                }) != text.end();
        }

        bool MatchesCatalogSearch(
            std::string_view name,
            std::string_view id,
            const char* query) {

            const std::string_view filter = query != nullptr
                ? std::string_view(query)
                : std::string_view{};
            return ContainsInsensitive(name, filter) ||
                ContainsInsensitive(id, filter);
        }
#endif
    } // namespace

    void RuntimeBindingWorkspaceController::DrawCatalogWindow(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Catalog###RuntimeBinding/Catalog")) {
            ImGui::End();
            return;
        }

        PanelTitle(
            "Variables & Events",
            "Project-owned IDs available to components and tools");
        if (ToggleButton(
                "All",
                "RuntimeBindingFilterAll",
                catalogFilter_ == CatalogFilter::All,
                ImVec2(58.0f, 26.0f))) {
            catalogFilter_ = CatalogFilter::All;
        }
        ImGui::SameLine();
        if (ToggleButton(
                "Variables",
                "RuntimeBindingFilterVariables",
                catalogFilter_ == CatalogFilter::Variables,
                ImVec2(84.0f, 26.0f))) {
            catalogFilter_ = CatalogFilter::Variables;
        }
        ImGui::SameLine();
        if (ToggleButton(
                "Events",
                "RuntimeBindingFilterEvents",
                catalogFilter_ == CatalogFilter::Events,
                ImVec2(72.0f, 26.0f))) {
            catalogFilter_ = CatalogFilter::Events;
        }
        ImGui::Separator();

        const BINDING::Catalog& catalog = document_.Catalog();
        size_t visibleCount = 0u;
        std::string deleteRequestedId{};
        if (catalogFilter_ != CatalogFilter::Events) {
            for (const BINDING::VariableDefinition& variable :
                 catalog.variables) {
                if (!MatchesCatalogSearch(
                        variable.name,
                        variable.id,
                        searchBuffer_.data())) {
                    continue;
                }
                ++visibleCount;
                ImGui::PushID(variable.id.c_str());
                const bool selected =
                    selectionKind_ == SelectionKind::Variable &&
                    selectedItemId_ == variable.id;
                if (ImGui::Selectable(
                        (variable.name + "##row").c_str(),
                        selected,
                        ImGuiSelectableFlags_AllowDoubleClick,
                        ImVec2(0.0f, 34.0f))) {
                    SelectItem(variable.id);
                }
                ImGui::SetCursorPosY(
                    ImGui::GetCursorPosY() -
                    ImGui::GetTextLineHeightWithSpacing());
                ImGui::TextDisabled(
                    "%s",
                    std::string(BINDING::ToString(
                        variable.defaultValue.type)).c_str());
                if (ImGui::BeginPopupContextItem("CatalogItemContext")) {
                    if (ImGui::MenuItem("Delete")) {
                        deleteRequestedId = variable.id;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }

        if (catalogFilter_ != CatalogFilter::Variables) {
            for (const BINDING::EventDefinition& event : catalog.events) {
                if (!MatchesCatalogSearch(
                        event.name,
                        event.id,
                        searchBuffer_.data())) {
                    continue;
                }
                ++visibleCount;
                ImGui::PushID(event.id.c_str());
                const bool selected =
                    selectionKind_ == SelectionKind::Event &&
                    selectedItemId_ == event.id;
                if (ImGui::Selectable(
                        (event.name + "##row").c_str(),
                        selected,
                        ImGuiSelectableFlags_AllowDoubleClick,
                        ImVec2(0.0f, 34.0f))) {
                    SelectItem(event.id);
                }
                const std::string eventType = event.hasPayload
                    ? "Event<" +
                        std::string(BINDING::ToString(event.payloadType)) +
                        ">"
                    : "Event";
                ImGui::SetCursorPosY(
                    ImGui::GetCursorPosY() -
                    ImGui::GetTextLineHeightWithSpacing());
                ImGui::TextDisabled(
                    "%s",
                    eventType.c_str());
                if (ImGui::BeginPopupContextItem("CatalogItemContext")) {
                    if (ImGui::MenuItem("Delete")) {
                        deleteRequestedId = event.id;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }

        if (visibleCount == 0u) {
            EmptyState(
                searchBuffer_[0] == '\0'
                    ? "No bindings yet"
                    : "No matching bindings",
                searchBuffer_[0] == '\0'
                    ? "Use Add in the toolbar to create a variable or event."
                    : "Try another name or stable ID.");
        }
        ImGui::End();

        if (!deleteRequestedId.empty()) {
            const RuntimeBindingRemovalResult removal =
                RemoveItem(scene, deleteRequestedId, false);
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
