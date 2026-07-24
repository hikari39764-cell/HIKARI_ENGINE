#include "Editor/SystemAuthoring/HIKARI_AddSystemPopup.h"

#include <algorithm>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

#if defined(HIKARI_WITH_EDITOR)
        constexpr const char* kPopupId =
            "Add System###HikariAddSystemPopup";

        bool CanAddSystem(
            const SceneSystemAuthoringRow& row,
            const SystemAuthoringRegistry& authoringRegistry) {

            const SystemAuthoringDescriptor* authoring =
                authoringRegistry.Find(row.systemId);
            const bool sceneInstallable = authoring &&
                (authoring->scope == SystemAuthoringScope::ComponentOwned ||
                    authoring->scope == SystemAuthoringScope::SceneSettings ||
                    authoring->scope == SystemAuthoringScope::DedicatedTool);
            return !IsSceneSystemInstalled(row) &&
                row.runtimeInfo != nullptr &&
                sceneInstallable;
        }
#endif
    }

    void AddSystemPopup::Open() {
        filter_.fill('\0');
        selectedSystemId_.clear();
        openRequested_ = true;
    }

    std::optional<std::string> AddSystemPopup::Draw(
        const std::vector<SceneSystemAuthoringRow>& rows,
        const SystemAuthoringRegistry& authoringRegistry) {

#if defined(HIKARI_WITH_EDITOR)
        if (openRequested_) {
            ImGui::OpenPopup(kPopupId);
            openRequested_ = false;
        }

        ImGui::SetNextWindowSize(
            ImVec2(620.0f, 460.0f),
            ImGuiCond_Appearing);
        if (!ImGui::BeginPopup(
                kPopupId,
                ImGuiWindowFlags_NoSavedSettings)) {
            return std::nullopt;
        }

        ImGui::SeparatorText("Add System");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##AddSystemFilter",
            "Search systems...",
            filter_.data(),
            filter_.size());

        size_t availableCount = 0;
        const float footerHeight =
            ImGui::GetFrameHeightWithSpacing() +
            ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginTable(
                "AddSystemCatalog",
                2,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, -footerHeight))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("System", 0, 0.68f);
            ImGui::TableSetupColumn("Feature", 0, 0.32f);
            ImGui::TableHeadersRow();

            for (const SceneSystemAuthoringRow& row : rows) {
                if (!CanAddSystem(row, authoringRegistry) ||
                    !MatchesSceneSystemFilter(row, filter_.data())) {
                    continue;
                }
                ++availableCount;
                const bool selected =
                    selectedSystemId_ == row.systemId;
                const char* displayName = row.runtimeInfo->displayName.c_str();

                ImGui::PushID(row.systemId.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(
                        displayName,
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedSystemId_ = row.systemId;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        const std::string result = selectedSystemId_;
                        ImGui::CloseCurrentPopup();
                        ImGui::PopID();
                        ImGui::EndTable();
                        ImGui::EndPopup();
                        return result;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(
                    "%s",
                    row.runtimeInfo->featureId.empty()
                        ? "Uncategorized"
                        : row.runtimeInfo->featureId.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (availableCount == 0) {
            ImGui::TextDisabled("No matching systems.");
        }

        std::optional<std::string> result{};
        ImGui::BeginDisabled(selectedSystemId_.empty());
        if (ImGui::Button("Add", ImVec2(96.0f, 0.0f))) {
            result = selectedSystemId_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return result;
#else
        (void)rows;
        (void)authoringRegistry;
        return std::nullopt;
#endif
    }

} // namespace HIKARI::EDITOR
