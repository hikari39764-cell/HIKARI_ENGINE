#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <memory>
#include <string>

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void RuntimeBindingWorkspaceController::DrawLiveReferencesWindow(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                "Live & References###RuntimeBinding/LiveReferences")) {
            ImGui::End();
            return;
        }

        const BINDING::VariableDefinition* variable = SelectedVariable();
        const BINDING::EventDefinition* event = SelectedEvent();
        if (variable == nullptr && event == nullptr) {
            EmptyState(
                "Select a binding",
                "Live values, test events, and references appear here.");
            ImGui::End();
            return;
        }

        if (previewDraftItemId_ != selectedItemId_) {
            if (variable != nullptr) {
                BINDING::Value current{};
                previewVariableValue_ =
                    scene.GetRuntimeBindingStore().ReadVariable(
                        variable->id, current)
                    ? current
                    : variable->defaultValue;
            } else if (event != nullptr) {
                previewEventPayload_ =
                    BINDING::MakeDefaultValue(event->payloadType);
            }
            previewDraftItemId_ = selectedItemId_;
        }

        if (ImGui::BeginTabBar("RuntimeBindingLiveTabs")) {
            if (ImGui::BeginTabItem("Live")) {
                if (variable != nullptr) {
                    PanelTitle(
                        variable->name.c_str(),
                        "Runtime snapshot and session-only preview override");
                    const std::shared_ptr<const BINDING::Snapshot> snapshot =
                        scene.GetRuntimeBindingStore().GetPublishedSnapshot();
                    const BINDING::Value* current = snapshot != nullptr
                        ? snapshot->Find(variable->id)
                        : nullptr;
                    BINDING::Value displayed = current != nullptr
                        ? *current
                        : variable->defaultValue;

                    if (BeginMetricTable("RuntimeBindingLiveMetrics", 150.0f)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted("Snapshot revision");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text(
                            "%llu",
                            static_cast<unsigned long long>(
                                snapshot != nullptr
                                    ? snapshot->revision
                                    : 0u));

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted("Writer");
                        ImGui::TableSetColumnIndex(1);
                        const std::string writer(
                            scene.GetRuntimeBindingStore().GetWriter(
                                variable->id));
                        ImGui::TextUnformatted(
                            writer.empty() ? "Unclaimed" : writer.c_str());
                        EndPropertyTable();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Current value");
                    ImGui::BeginDisabled();
                    DrawRuntimeBindingValueEditor(
                        "##RuntimeBindingCurrentValue",
                        displayed,
                        variable);
                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::TextDisabled("Preview value");
                    DrawRuntimeBindingValueEditor(
                        "##RuntimeBindingPreviewValue",
                        previewVariableValue_,
                        variable);
                    if (IconTextButton(
                            EditorGlyph::Play,
                            "Set Preview",
                            "RuntimeBindingSetPreview",
                            EditorButtonTone::Primary,
                            ImVec2(0.0f, 28.0f),
                            "Write through the editor preview writer")) {
                        PreviewSetVariable(
                            scene,
                            variable->id,
                            WriteRuntimeBindingEditorValue(
                                previewVariableValue_),
                            statusMessage_);
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(
                        !previewVariableIds_.contains(variable->id));
                    if (ActionButton(
                            "Release",
                            "RuntimeBindingReleasePreview",
                            EditorButtonTone::Neutral,
                            ImVec2(0.0f, 28.0f),
                            "Release the editor writer without claiming a new value")) {
                        PreviewReset(
                            scene, variable->id, statusMessage_);
                    }
                    ImGui::EndDisabled();
                } else {
                    PanelTitle(
                        event->name.c_str(),
                        "Emit one test signal into the current runtime frame");
                    if (event->hasPayload) {
                        ImGui::TextDisabled("Test payload");
                        DrawRuntimeBindingValueEditor(
                            "##RuntimeBindingEventPayload",
                            previewEventPayload_);
                    } else {
                        StatusText(
                            "This event has no payload",
                            EditorStatusTone::Normal);
                    }
                    if (IconTextButton(
                            EditorGlyph::Play,
                            "Emit Test Event",
                            "RuntimeBindingEmitEvent",
                            EditorButtonTone::Primary,
                            ImVec2(0.0f, 28.0f),
                            "Emit once; the event is not serialized")) {
                        const nlohmann::json payload =
                            WriteRuntimeBindingEditorValue(
                                previewEventPayload_);
                        PreviewEmitEvent(
                            scene,
                            event->id,
                            event->hasPayload ? &payload : nullptr,
                            statusMessage_);
                    }

                    ImGui::Spacing();
                    ImGui::SeparatorText("Current frame events");
                    const std::vector<const BINDING::EventRecord*> records =
                        scene.GetRuntimeBindingStore().FindEvents(event->id);
                    if (records.empty()) {
                        EmptyState(
                            "No events this frame",
                            "Emit a test event or let runtime logic publish one.");
                    } else if (ImGui::BeginTable(
                            "RuntimeBindingEventRecords",
                            4,
                            ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerV |
                                ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Sequence");
                        ImGui::TableSetupColumn("Frame");
                        ImGui::TableSetupColumn("Source");
                        ImGui::TableSetupColumn("Payload");
                        ImGui::TableHeadersRow();
                        const size_t first = records.size() > 64u
                            ? records.size() - 64u
                            : 0u;
                        for (size_t index = first;
                            index < records.size(); ++index) {
                            const BINDING::EventRecord& record =
                                *records[index];
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text(
                                "%llu",
                                static_cast<unsigned long long>(
                                    record.sequence));
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(
                                "%llu",
                                static_cast<unsigned long long>(
                                    record.frameIndex));
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text(
                                "%llu",
                                static_cast<unsigned long long>(
                                    record.sourceObject.ToValue()));
                            ImGui::TableSetColumnIndex(3);
                            if (record.hasPayload) {
                                ImGui::TextUnformatted(
                                    WriteRuntimeBindingEditorValue(
                                        record.payload).dump().c_str());
                            } else {
                                ImGui::TextDisabled("None");
                            }
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("References")) {
                const std::vector<RuntimeBindingReference> references =
                    GetReferences(selectedItemId_);
                PanelTitle(
                    "References",
                    "Editor-authored slots that use this stable ID");
                if (references.empty()) {
                    EmptyState(
                        "No references found",
                        referenceProvider_
                            ? "No use was found in the current scene and loaded materials. Other project scenes may still reference this stable ID."
                            : "Reference indexing is not connected yet.");
                } else if (ImGui::BeginTable(
                        "RuntimeBindingReferences",
                        4,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Target");
                    ImGui::TableSetupColumn("Slot");
                    ImGui::TableSetupColumn("Access");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableHeadersRow();
                    for (const RuntimeBindingReference& reference :
                         references) {
                        ImGui::PushID(
                            (reference.targetKind + ":" +
                             reference.targetId + ":" +
                             reference.slotId).c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const std::string target =
                            reference.targetLabel.empty()
                            ? reference.targetId
                            : reference.targetLabel;
                        if (ImGui::Selectable(
                                target.c_str(),
                                false,
                                ImGuiSelectableFlags_SpanAllColumns) &&
                            referenceRevealCallback_) {
                            referenceRevealCallback_(reference);
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(
                            reference.slotLabel.empty()
                                ? reference.slotId.c_str()
                                : reference.slotLabel.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(reference.access.c_str());
                        ImGui::TableSetColumnIndex(3);
                        StatusText(
                            reference.status.c_str(),
                            reference.status == "resolved"
                                ? EditorStatusTone::Ready
                                : EditorStatusTone::Warning);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI::EDITOR
