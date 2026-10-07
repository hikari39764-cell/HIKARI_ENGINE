#include "Core/Text/HIKARI_Localization.h"
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

void RuntimeBindingWorkspaceController::DrawLiveReferencesWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(
            TEXT::LocalizeWindow("editor.ui.live.references.f80382", "RuntimeBinding/LiveReferences").c_str())) {
        ImGui::End();
        return;
    }

    const BINDING::VariableDefinition* variable = SelectedVariable();
    const BINDING::EventDefinition* event = SelectedEvent();
    if (variable == nullptr && event == nullptr) {
        EmptyState(TEXT::Localize("editor.ui.select.a.binding.658ec5").c_str(),
                   TEXT::Localize("editor.ui.live.values.test.events.and.references.appear.here.acf72b").c_str());
        ImGui::End();
        return;
    }

    if (previewDraftItemId_ != selectedItemId_) {
        if (variable != nullptr) {
            BINDING::Value current{};
            previewVariableValue_ =
                scene.GetRuntimeBindingStore().ReadVariable(variable->id, current) ? current : variable->defaultValue;
        } else if (event != nullptr) {
            previewEventPayload_ = BINDING::MakeDefaultValue(event->payloadType);
        }
        previewDraftItemId_ = selectedItemId_;
    }

    if (ImGui::BeginTabBar("RuntimeBindingLiveTabs")) {
        if (ImGui::BeginTabItem(TEXT::LocalizeWindow("editor.ui.live.65c821", "Live").c_str())) {
            if (variable != nullptr) {
                PanelTitle(
                    variable->name.c_str(),
                    TEXT::Localize("editor.ui.runtime.snapshot.and.session.only.preview.override.4d830e").c_str());
                const std::shared_ptr<const BINDING::Snapshot> snapshot =
                    scene.GetRuntimeBindingStore().GetPublishedSnapshot();
                const BINDING::Value* current = snapshot != nullptr ? snapshot->Find(variable->id) : nullptr;
                BINDING::Value displayed = current != nullptr ? *current : variable->defaultValue;

                if (BeginMetricTable("RuntimeBindingLiveMetrics", 150.0f)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(TEXT::Localize("editor.ui.snapshot.revision.145181").c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(snapshot != nullptr ? snapshot->revision : 0u));

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(TEXT::Localize("editor.ui.writer.5f4358").c_str());
                    ImGui::TableSetColumnIndex(1);
                    const std::string writer(scene.GetRuntimeBindingStore().GetWriter(variable->id));
                    ImGui::TextUnformatted(writer.empty() ? TEXT::Localize("editor.ui.unclaimed.fca0eb").c_str()
                                                          : writer.c_str());
                    EndPropertyTable();
                }

                ImGui::Spacing();
                ImGui::TextDisabled(TEXT::Localize("editor.ui.current.value.ed2757").c_str());
                ImGui::BeginDisabled();
                DrawRuntimeBindingValueEditor("##RuntimeBindingCurrentValue", displayed, variable);
                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::TextDisabled(TEXT::Localize("editor.ui.preview.value.709455").c_str());
                DrawRuntimeBindingValueEditor("##RuntimeBindingPreviewValue", previewVariableValue_, variable);
                if (IconTextButton(
                        EditorGlyph::Play, TEXT::Localize("editor.ui.set.preview.aa5e52").c_str(),
                        "RuntimeBindingSetPreview", EditorButtonTone::Primary, ImVec2(0.0f, 28.0f),
                        TEXT::Localize("editor.ui.write.through.the.editor.preview.writer.d78567").c_str())) {
                    PreviewSetVariable(scene, variable->id, WriteRuntimeBindingEditorValue(previewVariableValue_),
                                       statusMessage_);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!previewVariableIds_.contains(variable->id));
                if (ActionButton(
                        TEXT::Localize("editor.ui.release.d41f56").c_str(), "RuntimeBindingReleasePreview",
                        EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                        TEXT::Localize("editor.ui.release.the.editor.writer.without.claiming.a.new.value.02dd52")
                            .c_str())) {
                    PreviewReset(scene, variable->id, statusMessage_);
                }
                ImGui::EndDisabled();
            } else {
                PanelTitle(
                    event->name.c_str(),
                    TEXT::Localize("editor.ui.emit.one.test.signal.into.the.current.runtime.frame.2cf543").c_str());
                if (event->hasPayload) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.test.payload.fc5428").c_str());
                    DrawRuntimeBindingValueEditor("##RuntimeBindingEventPayload", previewEventPayload_);
                } else {
                    StatusText(TEXT::Localize("editor.ui.this.event.has.no.payload.daca11").c_str(),
                               EditorStatusTone::Normal);
                }
                if (IconTextButton(EditorGlyph::Play, TEXT::Localize("editor.ui.emit.test.event.36093e").c_str(),
                                   "RuntimeBindingEmitEvent", EditorButtonTone::Primary, ImVec2(0.0f, 28.0f),
                                   TEXT::Localize("editor.ui.emit.once.the.event.is.not.serialized.a82087").c_str())) {
                    const nlohmann::json payload = WriteRuntimeBindingEditorValue(previewEventPayload_);
                    PreviewEmitEvent(scene, event->id, event->hasPayload ? &payload : nullptr, statusMessage_);
                }

                ImGui::Spacing();
                ImGui::SeparatorText(TEXT::Localize("editor.ui.current.frame.events.ce1cbb").c_str());
                const std::vector<const BINDING::EventRecord*> records =
                    scene.GetRuntimeBindingStore().FindEvents(event->id);
                if (records.empty()) {
                    EmptyState(
                        TEXT::Localize("editor.ui.no.events.this.frame.7e32dc").c_str(),
                        TEXT::Localize("editor.ui.emit.a.test.event.or.let.runtime.logic.publish.one.e47e4c").c_str());
                } else if (ImGui::BeginTable("RuntimeBindingEventRecords", 4,
                                             ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                 ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Sequence");
                    ImGui::TableSetupColumn("Frame");
                    ImGui::TableSetupColumn("Source");
                    ImGui::TableSetupColumn("Payload");
                    ImGui::TableHeadersRow();
                    const size_t first = records.size() > 64u ? records.size() - 64u : 0u;
                    for (size_t index = first; index < records.size(); ++index) {
                        const BINDING::EventRecord& record = *records[index];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%llu", static_cast<unsigned long long>(record.sequence));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%llu", static_cast<unsigned long long>(record.frameIndex));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%llu", static_cast<unsigned long long>(record.sourceObject.ToValue()));
                        ImGui::TableSetColumnIndex(3);
                        if (record.hasPayload) {
                            ImGui::TextUnformatted(WriteRuntimeBindingEditorValue(record.payload).dump().c_str());
                        } else {
                            ImGui::TextDisabled(TEXT::Localize("ui.input.none").c_str());
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TEXT::LocalizeWindow("editor.ui.references.5d20d0", "References").c_str())) {
            const std::vector<RuntimeBindingReference> references = GetReferences(selectedItemId_);
            PanelTitle(TEXT::Localize("editor.ui.references.5d20d0").c_str(),
                       TEXT::Localize("editor.ui.editor.authored.slots.that.use.this.stable.id.96ae0f").c_str());
            if (references.empty()) {
                EmptyState(
                    TEXT::Localize("editor.ui.no.references.found.7b8efc").c_str(),
                    referenceProvider_
                        ? TEXT::Localize(
                              "editor.ui.no.use.was.found.in.the.current.scene.and.loaded.materials.other.ea9fa8")
                              .c_str()
                        : TEXT::Localize("editor.ui.reference.indexing.is.not.connected.yet.1b4c90").c_str());
            } else if (ImGui::BeginTable("RuntimeBindingReferences", 4,
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Target");
                ImGui::TableSetupColumn("Slot");
                ImGui::TableSetupColumn("Access");
                ImGui::TableSetupColumn("Status");
                ImGui::TableHeadersRow();
                for (const RuntimeBindingReference& reference : references) {
                    ImGui::PushID((reference.targetKind + ":" + reference.targetId + ":" + reference.slotId).c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const std::string target =
                        reference.targetLabel.empty() ? reference.targetId : reference.targetLabel;
                    if (ImGui::Selectable(target.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) &&
                        referenceRevealCallback_) {
                        referenceRevealCallback_(reference);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(reference.slotLabel.empty() ? reference.slotId.c_str()
                                                                       : reference.slotLabel.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(reference.access.c_str());
                    ImGui::TableSetColumnIndex(3);
                    StatusText(reference.status.c_str(),
                               reference.status == "resolved" ? EditorStatusTone::Ready : EditorStatusTone::Warning);
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
