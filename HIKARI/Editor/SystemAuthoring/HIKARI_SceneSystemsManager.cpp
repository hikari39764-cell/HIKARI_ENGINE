#include "Editor/SystemAuthoring/HIKARI_SceneSystemsManager.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "Editor/SystemAuthoring/HIKARI_SceneSystemAuthoringModel.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

#if defined(HIKARI_WITH_EDITOR)
        constexpr const char* kPopupId =
            "System Manager###HikariSceneSystemsManager";
        constexpr const char* kDiscardPopupId =
            "Discard Changes?###HikariSystemManagerDiscard";

        const SceneSystemAuthoringRow* FindRow(
            const std::vector<SceneSystemAuthoringRow>& rows,
            std::string_view systemId) {

            const auto found = std::find_if(
                rows.begin(),
                rows.end(),
                [systemId](const SceneSystemAuthoringRow& row) {
                    return row.systemId == systemId;
                });
            return found != rows.end() ? &*found : nullptr;
        }

        const SceneSystemAuthoringRow* FindFirstInstalledRow(
            const std::vector<SceneSystemAuthoringRow>& rows) {

            const auto found = std::find_if(
                rows.begin(),
                rows.end(),
                [](const SceneSystemAuthoringRow& row) {
                    return IsSceneSystemInstalled(row);
                });
            return found != rows.end() ? &*found : nullptr;
        }

        ImVec4 StatusColor(
            const SceneSystemAuthoringRow& row,
            const SceneDocument& document) {

            if (!row.runtimeInfo) {
                return ImVec4(1.0f, 0.36f, 0.32f, 1.0f);
            }
            if (!IsSceneSystemEffectivelyEnabled(row, document)) {
                return ImVec4(0.48f, 0.50f, 0.54f, 1.0f);
            }
            if (!row.scheduled) {
                return ImVec4(1.0f, 0.62f, 0.24f, 1.0f);
            }
            return ImVec4(0.34f, 0.86f, 0.48f, 1.0f);
        }

        const char* SourceLabel(
            const SceneSystemAuthoringRow& row) noexcept {

            if (row.projectDefault) {
                return "Project";
            }
            if (IsSceneSystemAutomatic(row)) {
                return "Auto";
            }
            if (row.requiredComponentCount > 0) {
                return "Override";
            }
            return "Scene";
        }
#endif
    }

    void SceneSystemsManager::Open() {
        openRequested_ = true;
    }

    SceneSystemsManagerResult SceneSystemsManager::Draw(
        DocumentSceneBase& scene,
        const SystemAuthoringRegistry& authoringRegistry,
        EditorToolHost& toolHost) {

        SceneSystemsManagerResult result{};
#if defined(HIKARI_WITH_EDITOR)
        if (openRequested_) {
            ImGui::OpenPopup(kPopupId);
            openRequested_ = false;
        }

        SceneDocument& document = scene.GetSceneDocument();
        const SystemTypeRegistry& typeRegistry =
            scene.GetSystemTypeRegistry();
        const std::vector<SceneSystemData> defaults =
            scene.CreateProjectDefaultSceneSystems();
        const std::vector<SceneSystemAuthoringRow> rows =
            BuildSceneSystemAuthoringRows(
                document,
                typeRegistry,
                scene.GetComponentSystemPolicy(),
                scene.GetSystemScheduler(),
                defaults);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 managerSize{
            (std::min)(1100.0f, viewport->WorkSize.x * 0.90f),
            (std::min)(760.0f, viewport->WorkSize.y * 0.90f)
        };
        ImGui::SetNextWindowPos(
            viewport->GetCenter(),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(managerSize, ImGuiCond_Appearing);

        if (!ImGui::BeginPopupModal(
                kPopupId,
                nullptr,
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings)) {
            return result;
        }

        auto selectRow = [&](const SceneSystemAuthoringRow& row) {
            SceneSystemData entry = MakeEditableSceneSystemEntry(
                row,
                document,
                typeRegistry);
            inspector_.Select(
                std::move(entry),
                row.runtimeInfo
                    ? row.runtimeInfo->displayName
                    : row.systemId);
            selectedSystemId_ = row.systemId;
        };

        const SceneSystemAuthoringRow* selectedRow =
            FindRow(rows, selectedSystemId_);
        if (selectedRow && !IsSceneSystemInstalled(*selectedRow)) {
            selectedRow = nullptr;
        }
        if (!selectedRow) {
            selectedRow = FindFirstInstalledRow(rows);
            if (selectedRow) {
                selectRow(*selectedRow);
            } else {
                selectedSystemId_.clear();
                inspector_.Clear();
            }
        } else if (!inspector_.HasTarget() ||
            inspector_.GetTargetSystemId() != selectedSystemId_) {
            selectRow(*selectedRow);
        }

        ImGui::TextUnformatted("Installed Systems");
        const float closeWidth = 88.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX((std::max)(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - closeWidth));
        if (ImGui::Button("Close", ImVec2(closeWidth, 0.0f))) {
            if (inspector_.IsDirty()) {
                pendingClose_ = true;
                pendingSystemId_.clear();
                discardPromptRequested_ = true;
            } else {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();

        const float listWidth = (std::clamp)(
            managerSize.x * 0.31f,
            290.0f,
            360.0f);
        (void)ImGui::BeginChild(
            "SystemManagerList",
            ImVec2(listWidth, 0.0f),
            true);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##InstalledSystemFilter",
            "Search installed...",
            filter_.data(),
            filter_.size());

        ImGui::BeginDisabled(scene.IsRuntimePlayActive() ||
            inspector_.IsDirty());
        if (ImGui::Button("+ Add System", ImVec2(-1.0f, 0.0f))) {
            addSystemPopup_.Open();
        }
        ImGui::EndDisabled();

        if (ImGui::BeginTable(
                "InstalledSystemList",
                3,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(
                "State",
                ImGuiTableColumnFlags_WidthFixed,
                22.0f);
            ImGui::TableSetupColumn("System", 0, 1.0f);
            ImGui::TableSetupColumn(
                "Source",
                ImGuiTableColumnFlags_WidthFixed,
                66.0f);

            for (const SceneSystemAuthoringRow& row : rows) {
                if (!IsSceneSystemInstalled(row) ||
                    !MatchesSceneSystemFilter(row, filter_.data())) {
                    continue;
                }

                ImGui::PushID(row.systemId.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(StatusColor(row, document), "●");

                ImGui::TableSetColumnIndex(1);
                const bool selected = selectedSystemId_ == row.systemId;
                const char* displayName = row.runtimeInfo
                    ? row.runtimeInfo->displayName.c_str()
                    : row.systemId.c_str();
                if (ImGui::Selectable(
                        displayName,
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                    if (selectedSystemId_ != row.systemId) {
                        if (inspector_.IsDirty()) {
                            pendingClose_ = false;
                            pendingSystemId_ = row.systemId;
                            discardPromptRequested_ = true;
                        } else {
                            selectRow(row);
                        }
                    }
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", SourceLabel(row));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        (void)ImGui::BeginChild(
            "SystemManagerInspector",
            ImVec2(0.0f, 0.0f),
            true);
        selectedRow = FindRow(rows, selectedSystemId_);
        if (selectedRow && IsSceneSystemInstalled(*selectedRow)) {
            const SystemSettingsInspectorResult inspectorResult =
                inspector_.Draw(
                    *selectedRow,
                    scene,
                    typeRegistry,
                    authoringRegistry,
                    toolHost,
                    scene.IsRuntimePlayActive());
            if (inspectorResult.action !=
                    SystemSettingsInspectorAction::None) {
                result.before = document.systems;
                if (inspectorResult.action ==
                        SystemSettingsInspectorAction::Apply) {
                    UpsertSceneSystem(
                        document,
                        inspectorResult.system);
                    result.label = "Configure Scene System";
                } else {
                    RemoveSceneSystemOverride(
                        document,
                        inspectorResult.system.systemId);
                    result.label = selectedRow->requiredComponentCount > 0
                        ? "Use Automatic Scene System"
                        : "Remove Scene System";
                    inspector_.Clear();
                    if (selectedRow->requiredComponentCount == 0) {
                        selectedSystemId_.clear();
                    }
                }
                result.changed = true;
            }
        }
        ImGui::EndChild();

        const std::optional<std::string> addedSystemId =
            addSystemPopup_.Draw(rows, authoringRegistry);
        if (addedSystemId && !result.changed) {
            const SceneSystemAuthoringRow* row =
                FindRow(rows, *addedSystemId);
            if (row) {
                result.before = document.systems;
                SceneSystemData entry = MakeEditableSceneSystemEntry(
                    *row,
                    document,
                    typeRegistry);
                entry.enabled = true;
                UpsertSceneSystem(document, entry);
                inspector_.Select(
                    entry,
                    row->runtimeInfo
                        ? row->runtimeInfo->displayName
                        : row->systemId);
                selectedSystemId_ = row->systemId;
                result.label = "Add Scene System";
                result.changed = true;
            }
        }

        if (discardPromptRequested_) {
            ImGui::OpenPopup(kDiscardPopupId);
            discardPromptRequested_ = false;
        }
        bool closeManager = false;
        ImGui::SetNextWindowSize(
            ImVec2(410.0f, 140.0f),
            ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                kDiscardPopupId,
                nullptr,
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted("Discard unapplied system changes?");
            if (ImGui::Button("Discard", ImVec2(96.0f, 0.0f))) {
                inspector_.Revert();
                if (pendingClose_) {
                    closeManager = true;
                } else if (const SceneSystemAuthoringRow* row =
                        FindRow(rows, pendingSystemId_)) {
                    selectRow(*row);
                }
                pendingClose_ = false;
                pendingSystemId_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
                pendingClose_ = false;
                pendingSystemId_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (closeManager) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
        (void)authoringRegistry;
        (void)toolHost;
#endif
        return result;
    }

} // namespace HIKARI::EDITOR
