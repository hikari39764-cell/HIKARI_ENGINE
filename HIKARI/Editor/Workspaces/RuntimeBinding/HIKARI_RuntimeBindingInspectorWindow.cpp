#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        template<size_t Size>
        void CopyToBuffer(
            std::array<char, Size>& buffer,
            std::string_view value) {

            buffer.fill('\0');
            const size_t count = std::min(
                value.size(), buffer.size() - 1u);
            std::memcpy(buffer.data(), value.data(), count);
        }

        bool DrawValueTypeCombo(
            const char* id,
            BINDING::ValueType current,
            BINDING::ValueType& outType) {

            bool changed = false;
            const std::string preview(BINDING::ToString(current));
            if (ImGui::BeginCombo(id, preview.c_str())) {
                for (const BINDING::ValueType type : {
                        BINDING::ValueType::Bool,
                        BINDING::ValueType::Int,
                        BINDING::ValueType::Float,
                        BINDING::ValueType::Float2,
                        BINDING::ValueType::Float3,
                        BINDING::ValueType::Float4,
                        BINDING::ValueType::Color,
                    }) {
                    const bool selected = current == type;
                    const std::string label(BINDING::ToString(type));
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        outType = type;
                        changed = current != type;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
#endif
    } // namespace

    void RuntimeBindingWorkspaceController::DrawInspectorWindow(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Inspector###RuntimeBinding/Inspector")) {
            ImGui::End();
            return;
        }

        const BINDING::VariableDefinition* variable = SelectedVariable();
        const BINDING::EventDefinition* event = SelectedEvent();
        if (variable == nullptr && event == nullptr) {
            EmptyState(
                "Nothing selected",
                "Select a variable or event from the Catalog.");
            ImGui::End();
            return;
        }

        if (inspectorDraftItemId_ != selectedItemId_) {
            if (variable != nullptr) {
                CopyToBuffer(inspectorNameBuffer_, variable->name);
                CopyToBuffer(
                    inspectorDescriptionBuffer_,
                    variable->description);
                inspectorDefaultValue_ = variable->defaultValue;
                inspectorHasRange_ = variable->hasRange;
                inspectorMinimum_ = variable->minimum;
                inspectorMaximum_ = variable->maximum;
                inspectorStep_ = variable->step;
            } else {
                CopyToBuffer(inspectorNameBuffer_, event->name);
                CopyToBuffer(
                    inspectorDescriptionBuffer_,
                    event->description);
                inspectorHasPayload_ = event->hasPayload;
                inspectorPayloadType_ = event->payloadType;
            }
            inspectorDraftItemId_ = selectedItemId_;
        }

        PanelTitle(
            variable != nullptr ? "Variable" : "Event",
            variable != nullptr
                ? "A typed value shared through stable binding slots"
                : "A transient typed signal emitted by runtime logic");

        std::optional<std::pair<std::string, nlohmann::json>>
            pendingProperty{};
        if (BeginPropertyTable("RuntimeBindingInspectorProperties", 112.0f)) {
            PropertyLabel("Name");
            ImGui::InputText(
                "##Name",
                inspectorNameBuffer_.data(),
                inspectorNameBuffer_.size());
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                pendingProperty = {
                    "name",
                    std::string(inspectorNameBuffer_.data())
                };
            }

            PropertyLabel("Stable ID");
            ImGui::TextUnformatted(selectedItemId_.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stable after creation; used by C++ and assets");
            }
            ImGui::SameLine();
            if (ActionButton(
                    "Copy",
                    "RuntimeBindingCopyId",
                    EditorButtonTone::Quiet)) {
                ImGui::SetClipboardText(selectedItemId_.c_str());
            }

            if (variable != nullptr) {
                PropertyLabel("Type");
                BINDING::ValueType selectedType =
                    variable->defaultValue.type;
                if (DrawValueTypeCombo(
                        "##VariableType",
                        variable->defaultValue.type,
                        selectedType)) {
                    pendingProperty = {
                        "type",
                        std::string(BINDING::ToString(selectedType))
                    };
                }

                PropertyLabel("Default");
                const RuntimeBindingValueEditResult defaultEdit =
                    DrawRuntimeBindingValueEditor(
                        "##DefaultValue",
                        inspectorDefaultValue_,
                        variable);
                if (defaultEdit.committed) {
                    pendingProperty = {
                        "default",
                        WriteRuntimeBindingEditorValue(
                            inspectorDefaultValue_)
                    };
                }

                if (BINDING::IsNumeric(variable->defaultValue.type)) {
                    PropertyLabel("Use Range");
                    ImGui::Checkbox(
                        "##HasRange",
                        &inspectorHasRange_);
                    if (inspectorHasRange_) {
                        PropertyLabel("Minimum");
                        ImGui::DragFloat(
                            "##Minimum",
                            &inspectorMinimum_,
                            0.01f);
                        PropertyLabel("Maximum");
                        ImGui::DragFloat(
                            "##Maximum",
                            &inspectorMaximum_,
                            0.01f);
                        PropertyLabel("Step");
                        ImGui::DragFloat(
                            "##Step",
                            &inspectorStep_,
                            0.001f,
                            0.0001f,
                            100000.0f,
                            "%.4f");
                    }
                    PropertyLabel("Range Edit");
                    if (ActionButton(
                            "Apply",
                            "RuntimeBindingApplyRange",
                            EditorButtonTone::Neutral)) {
                        pendingProperty = {
                            "range",
                            {
                                { "enabled", inspectorHasRange_ },
                                { "minimum", inspectorMinimum_ },
                                { "maximum", inspectorMaximum_ },
                                { "step", inspectorStep_ },
                            }
                        };
                    }
                }
            } else {
                PropertyLabel("Payload");
                if (ImGui::Checkbox(
                        "##HasPayload",
                        &inspectorHasPayload_)) {
                    pendingProperty = {
                        "hasPayload",
                        inspectorHasPayload_
                    };
                }
                if (inspectorHasPayload_) {
                    PropertyLabel("Payload Type");
                    BINDING::ValueType selectedType =
                        inspectorPayloadType_;
                    if (DrawValueTypeCombo(
                            "##PayloadType",
                            inspectorPayloadType_,
                            selectedType)) {
                        inspectorPayloadType_ = selectedType;
                        pendingProperty = {
                            "payloadType",
                            std::string(BINDING::ToString(selectedType))
                        };
                    }
                }
            }
            EndPropertyTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Description");
        ImGui::InputTextMultiline(
            "##RuntimeBindingDescription",
            inspectorDescriptionBuffer_.data(),
            inspectorDescriptionBuffer_.size(),
            ImVec2(-1.0f, 84.0f));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            pendingProperty = {
                "description",
                std::string(inspectorDescriptionBuffer_.data())
            };
        }

        ImGui::Spacing();
        const std::vector<BINDING::CatalogDiagnostic> diagnostics =
            GetDiagnostics();
        bool hasItemDiagnostic = false;
        for (const BINDING::CatalogDiagnostic& diagnostic : diagnostics) {
            if (diagnostic.subjectId != selectedItemId_) {
                continue;
            }
            hasItemDiagnostic = true;
            StatusText(
                diagnostic.message.c_str(),
                diagnostic.severity ==
                    BINDING::CatalogDiagnosticSeverity::Error
                    ? EditorStatusTone::Error
                    : EditorStatusTone::Warning);
        }
        if (!hasItemDiagnostic) {
            StatusText("Definition is valid", EditorStatusTone::Ready);
        }

        ImGui::Spacing();
        const bool deleteRequested = IconTextButton(
            EditorGlyph::Delete,
            "Delete",
            "RuntimeBindingInspectorDelete",
            EditorButtonTone::Danger,
            ImVec2(0.0f, 28.0f),
            "Delete this runtime binding definition");
        ImGui::End();

        if (pendingProperty) {
            SetItemProperty(
                scene,
                selectedItemId_,
                pendingProperty->first,
                pendingProperty->second,
                statusMessage_);
        }
        if (deleteRequested) {
            const RuntimeBindingRemovalResult removal =
                RemoveItem(scene, selectedItemId_, false);
            statusMessage_ = removal.message;
            if (removal.confirmationRequired) {
                pendingDeleteItemId_ = selectedItemId_;
                pendingDeleteReferences_ = removal.references;
                requestDeleteDialog_ = true;
            }
        }
#else
        (void)scene;
#endif
    }

} // namespace HIKARI::EDITOR
