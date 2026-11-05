#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxLayerOrder.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
bool ContainsInsensitive(std::string_view text, std::string_view query) {
    if (query.empty())
        return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](unsigned char left, unsigned char right) {
                           return std::tolower(left) == std::tolower(right);
                       }) != text.end();
}

bool IsEmitterSelection(VfxWorkspaceSelectionKind kind) noexcept {
    return kind == VfxWorkspaceSelectionKind::Emitter || kind == VfxWorkspaceSelectionKind::Bindings ||
           kind == VfxWorkspaceSelectionKind::Link || kind == VfxWorkspaceSelectionKind::CurveKey ||
           kind == VfxWorkspaceSelectionKind::ColorStop || kind == VfxWorkspaceSelectionKind::BurstKey ||
           kind == VfxWorkspaceSelectionKind::SpecialOutput;
}

std::string LinkLabel(const VFX::VfxEmitterData& emitter, const VFX::VfxInputLinkData& link,
                      const VFX::VfxExposedInputData* input) {
    const VFX::VfxPropertySchema* schema = VFX::ResolveVfxPropertySchema(emitter, link.target);
    const std::string target =
        schema != nullptr ? std::string(VFX::ToString(schema->moduleKind)) + "." + std::string(schema->propertyId)
                          : link.target.moduleId + "." + link.target.propertyId;
    return (input != nullptr ? input->name : link.inputId) + "  ->  " + target;
}
} // namespace

void VfxWorkspaceController::DrawHierarchyWindow(AssetDatabase& database) {
    if (!ImGui::Begin((VfxUiText("Library & Layers") + "###Vfx/Hierarchy").c_str())) {
        ImGui::End();
        return;
    }

    if (ImGui::Selectable(document_.Asset().name.c_str(), selection_.kind == VfxWorkspaceSelectionKind::Asset)) {
        Select({VfxWorkspaceSelectionKind::Asset, "asset"});
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", VfxUiText("Effect settings: duration, looping and name").c_str());
    ImGui::TextDisabled(VfxUiText("%zu layers  |  %.2f s").c_str(), document_.Asset().emitters.size(),
                        document_.Asset().durationSeconds);
    if (!ImGui::BeginTabBar("VfxLibraryTabs")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabItem(VfxUiText("Templates").c_str())) {
        DrawTemplateLibrary();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(VfxUiText("Shared").c_str())) {
        DrawContentLibrary(database);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(VfxUiText("Assets").c_str())) {
        ImGui::TextDisabled(VfxUiText("VFX ASSETS").c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##VfxAssetSearch", VfxUiText("Filter assets").c_str(), assetSearch_.data(),
                                 assetSearch_.size());
        if (ImGui::BeginChild("VfxAssetCatalog", {0.0f, 0.0f}, true)) {
            for (const AssetRecord* record : database.CollectByType(AssetType::VfxEffect)) {
                if (record == nullptr ||
                    !ContainsInsensitive(GetAssetRecordDisplayName(*record), assetSearch_.data())) {
                    continue;
                }
                ImGui::PushID(record->guid.value.c_str());
                const bool selected = document_.GetAssetGuid() == record->guid;
                if (ImGui::Selectable(GetAssetRecordDisplayName(*record).c_str(), selected)) {
                    (void)RequestOpenAsset(database, record->guid, statusMessage_);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(TEXT::Localize("editor.ui.s.nclick.to.open.d627df").c_str(),
                                      record->guid.value.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::EndTabItem();
    }
    const auto layerFlags = selectLayersTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem(VfxUiText("Layers").c_str(), nullptr, layerFlags)) {
        selectLayersTab_ = false;
        ImGui::SeparatorText(VfxUiText("Effect layers").c_str());

        const bool inputsOpen =
            ImGui::TreeNodeEx(VfxUiText("Exposed Inputs").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::IsItemClicked()) {
            Select({VfxWorkspaceSelectionKind::Inputs, "asset"});
        }
        if (inputsOpen) {
            for (const VFX::VfxExposedInputData& input : document_.Asset().inputs) {
                ImGui::PushID(input.id.c_str());
                const std::string label = input.name + "  :  " + std::string(VFX::ToString(input.type));
                if (ImGui::Selectable(label.c_str(), selection_.kind == VfxWorkspaceSelectionKind::Input &&
                                                         selection_.targetId == input.id)) {
                    Select({VfxWorkspaceSelectionKind::Input, input.id});
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton(VfxUiText("+ Input").c_str())) {
                ImGui::OpenPopup("Add VFX Input");
            }
            if (ImGui::BeginPopup("Add VFX Input")) {
                for (const VFX::VfxValueType type : {
                         VFX::VfxValueType::Bool,
                         VFX::VfxValueType::Int,
                         VFX::VfxValueType::Float,
                         VFX::VfxValueType::Float2,
                         VFX::VfxValueType::Float3,
                         VFX::VfxValueType::Float4,
                         VFX::VfxValueType::Color,
                     }) {
                    if (ImGui::MenuItem(std::string(VFX::ToString(type)).c_str())) {
                        std::string id{};
                        (void)AddInput(type, {}, {}, id, statusMessage_);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(selection_.kind != VfxWorkspaceSelectionKind::Input);
            if (ImGui::SmallButton(VfxUiText("Delete Input").c_str())) {
                requestDeleteInput_ = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        ImGui::Separator();
        // Context actions can replace the document while rows are being drawn.
        const auto layerSnapshot = document_.Asset();
        DrawEmitterTree(layerSnapshot, {});
        ImGui::Spacing();
        if (ImGui::Button(TEXT::Localize("ui.vfx.layer.add").c_str()))
            ImGui::OpenPopup("AddLayer");
        if (ImGui::BeginPopup("AddLayer")) {
            struct Kind {
                VFX::VfxLayerKind kind;
                const char* label;
            };
            const Kind kinds[] = {{VFX::VfxLayerKind::Shape, "ui.vfx.layer.newShape"},
                                  {VFX::VfxLayerKind::Particles, "ui.vfx.layer.newParticles"},
                                  {VFX::VfxLayerKind::Group, "ui.vfx.layer.newGroup"},
                                  {VFX::VfxLayerKind::Adjustment, "ui.vfx.layer.newAdjustment"},
                                  {VFX::VfxLayerKind::Fluid, "ui.vfx.layer.newFluid"},
                                  {VFX::VfxLayerKind::FluidSource, "ui.vfx.layer.newFluidSource"}};
            const auto* selected = FindEmitter(selection_.emitterId);
            // 新しい層の親：組を選んでいれば組、他は選んだ層と同じ親。流体源は選んだ域の下か、親の属する域の中にだけ
            // 置け、流体域は域の中に置けない。
            const std::string sibling = selected == nullptr                          ? std::string{}
                                        : selected->kind == VFX::VfxLayerKind::Group ? selected->id
                                                                                     : selected->parentEmitterId;
            const bool selectedDomain = selected != nullptr && selected->kind == VFX::VfxLayerKind::Fluid;
            const bool insideDomain = FindFluidDomainUnder(sibling) != nullptr;
            for (const auto& entry : kinds) {
                const bool fluidSource = entry.kind == VFX::VfxLayerKind::FluidSource;
                const bool enabled = fluidSource ? selectedDomain || insideDomain
                                                 : entry.kind != VFX::VfxLayerKind::Fluid || !insideDomain;
                if (!ImGui::MenuItem(TEXT::Localize(entry.label).c_str(), nullptr, false, enabled))
                    continue;
                const std::string parent = fluidSource && selectedDomain ? selected->id : sibling;
                std::string id;
                (void)AddEmitter(parent, TEXT::Localize(entry.label), {}, id, statusMessage_, entry.kind);
            }
            ImGui::EndPopup();
        }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, focused)) {
            if (selection_.kind == VfxWorkspaceSelectionKind::Input)
                requestDeleteInput_ = true;
            else if (IsEmitterSelection(selection_.kind))
                requestDeleteEmitter_ = true;
        }
        if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_F, focused))
            (void)FocusSelectedEmitter(statusMessage_);

        if (requestDeleteEmitter_) {
            ImGui::OpenPopup(
                TEXT::LocalizeWindow("editor.ui.delete.vfx.emitter.c34a06", "Delete VFX Emitter?").c_str());
            requestDeleteEmitter_ = false;
        }
        if (ImGui::BeginPopupModal(
                TEXT::LocalizeWindow("editor.ui.delete.vfx.emitter.c34a06", "Delete VFX Emitter?").c_str(), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                VfxUiText(
                    TEXT::Localize("ui.vfx.text.Delete the selected emitter? Child emitters are promoted.").c_str())
                    .c_str());
            if (ImGui::Button(VfxUiText("Cancel").c_str()))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button(VfxUiText("Delete Emitter").c_str())) {
                (void)RemoveEmitter(selection_.emitterId, statusMessage_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (requestDeleteInput_) {
            ImGui::OpenPopup(TEXT::LocalizeWindow("editor.ui.delete.vfx.input.a649fc", "Delete VFX Input?").c_str());
            requestDeleteInput_ = false;
        }
        if (ImGui::BeginPopupModal(
                TEXT::LocalizeWindow("editor.ui.delete.vfx.input.a649fc", "Delete VFX Input?").c_str(), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                VfxUiText(TEXT::Localize("ui.vfx.text.Delete the input and all property links that use it?").c_str())
                    .c_str());
            if (ImGui::Button(VfxUiText("Cancel").c_str()))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button(VfxUiText("Delete Input").c_str())) {
                (void)RemoveInput(selection_.targetId, statusMessage_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::End();
}

void VfxWorkspaceController::DrawEmitterTree(const VFX::VfxAssetData& asset, std::string_view parentId) {
    const auto children = VFX::CollectVfxLayerChildren(asset, parentId);
    for (const VFX::VfxEmitterData* emitter : children) {
        ImGui::PushID(emitter->id.c_str());
        const bool emitterSelected = selection_.emitterId == emitter->id;

        const bool hidden = preview_.IsEmitterHidden(emitter->id);
        if (IconButton(
                hidden ? EditorGlyph::Hidden : EditorGlyph::Visible, "Visibility", EditorButtonTone::Quiet, {24, 24},
                TEXT::Localize(hidden ? "ui.vfx.hierarchy.showPreview" : "ui.vfx.hierarchy.hidePreview").c_str())) {
            preview_.SetEmitterHidden(emitter->id, !hidden);
        }
        ImGui::SameLine(0, 2);
        if (IconToggleButton(EditorGlyph::Solo, "Solo", preview_.soloEmitterId_ == emitter->id, {24, 24},
                             TEXT::Localize("ui.vfx.hud.solo").c_str())) {
            if (preview_.soloEmitterId_ == emitter->id)
                preview_.ClearSoloEmitter();
            else
                preview_.SetSoloEmitter(emitter->id);
        }
        ImGui::SameLine();

        const bool open = ImGui::TreeNodeEx(
            "Emitter",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                (emitterSelected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None),
            "%s%s%s", emitter->basic.enabled ? "" : "[Disabled] ",
            preview_.soloEmitterId_ == emitter->id ? "[Solo] " : "",
            (emitter->kind == VFX::VfxLayerKind::Adjustment ? "[FX] " + emitter->name : emitter->name).c_str());
        if (ImGui::IsItemClicked()) {
            Select({VfxWorkspaceSelectionKind::Emitter, emitter->id, emitter->id});
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("HIKARI_VFX_EMITTER", emitter->id.data(), emitter->id.size() + 1u);
            ImGui::TextUnformatted(emitter->name.c_str());
            ImGui::EndDragDropSource();
        }
        if (emitter->kind != VFX::VfxLayerKind::Adjustment && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIKARI_VFX_EMITTER")) {
                const std::string draggedId = static_cast<const char*>(payload->Data);
                std::string message{};
                if (!ReparentEmitter(draggedId, emitter->id, message)) {
                    statusMessage_ = message;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("EmitterContext")) {
            Select({VfxWorkspaceSelectionKind::Emitter, emitter->id, emitter->id});
            if (ImGui::MenuItem(TEXT::Localize("ui.vfx.hierarchy.addChild").c_str())) {
                std::string id{};
                (void)AddEmitter(emitter->id, {}, {}, id, statusMessage_);
            }
            if (ImGui::MenuItem(TEXT::Localize("ui.vfx.hierarchy.duplicate").c_str())) {
                std::string id{};
                (void)DuplicateEmitter(emitter->id, id, statusMessage_);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.up").c_str()))
                (void)ReorderEmitter(emitter->id, -1, statusMessage_);
            if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.down").c_str()))
                (void)ReorderEmitter(emitter->id, 1, statusMessage_);
            ImGui::Separator();
            if (ImGui::MenuItem(TEXT::Localize(hidden ? "ui.vfx.hierarchy.show" : "ui.vfx.hierarchy.hide").c_str())) {
                preview_.SetEmitterHidden(emitter->id, !hidden);
            }
            if (ImGui::MenuItem(TEXT::Localize(preview_.soloEmitterId_ == emitter->id ? "ui.vfx.hierarchy.clearSolo"
                                                                                      : "ui.vfx.hierarchy.solo")
                                    .c_str())) {
                if (preview_.soloEmitterId_ == emitter->id) {
                    preview_.ClearSoloEmitter();
                } else {
                    preview_.SetSoloEmitter(emitter->id);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TEXT::Localize("ui.vfx.hierarchy.delete").c_str(), nullptr, false,
                                document_.Asset().emitters.size() > 1u)) {
                requestDeleteEmitter_ = true;
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (!emitter->inputLinks.empty()) {
                const std::string bindingLabel = "Bindings (" + std::to_string(emitter->inputLinks.size()) + ")";
                const bool bindingsOpen = ImGui::TreeNodeEx(bindingLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemClicked()) {
                    Select({VfxWorkspaceSelectionKind::Bindings, emitter->id, emitter->id});
                }
                if (bindingsOpen) {
                    for (const VFX::VfxInputLinkData& link : emitter->inputLinks) {
                        const std::string label = LinkLabel(*emitter, link, FindInput(link.inputId));
                        if (ImGui::Selectable(label.c_str(), selection_.kind == VfxWorkspaceSelectionKind::Link &&
                                                                 selection_.targetId == link.id)) {
                            Select({
                                VfxWorkspaceSelectionKind::Link,
                                link.id,
                                emitter->id,
                                link.target.moduleId,
                                link.target.propertyId,
                            });
                        }
                    }
                    ImGui::TreePop();
                }
            }
            if (!emitter->spawn.burstTimeline.empty()) {
                const std::string burstLabel =
                    "Burst Timeline (" + std::to_string(emitter->spawn.burstTimeline.size()) + ")";
                const bool burstOpen =
                    ImGui::TreeNodeEx("BurstTimeline", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", burstLabel.c_str());
                if (ImGui::IsItemClicked()) {
                    Select({VfxWorkspaceSelectionKind::Emitter, emitter->id, emitter->id});
                }
                if (burstOpen) {
                    for (const VFX::VfxBurstKeyData& key : emitter->spawn.burstTimeline) {
                        const std::string label =
                            key.id + "  " + std::to_string(key.time) + "s x" + std::to_string(key.count);
                        if (ImGui::Selectable(label.c_str(), selection_.kind == VfxWorkspaceSelectionKind::BurstKey &&
                                                                 selection_.targetId == key.id)) {
                            Select({VfxWorkspaceSelectionKind::BurstKey, key.id, emitter->id});
                        }
                    }
                    ImGui::TreePop();
                }
            }
            if (!emitter->specialOutputs.empty()) {
                const std::string outputLabel =
                    "Special Outputs (" + std::to_string(emitter->specialOutputs.size()) + ")";
                const bool outputOpen =
                    ImGui::TreeNodeEx("SpecialOutputs", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", outputLabel.c_str());
                if (ImGui::IsItemClicked()) {
                    Select({VfxWorkspaceSelectionKind::Emitter, emitter->id, emitter->id});
                }
                if (outputOpen) {
                    for (const VFX::VfxSpecialOutputData& output : emitter->specialOutputs) {
                        const std::string label = output.id + "  [" + std::string(VFX::ToString(output.type)) + "]";
                        if (ImGui::Selectable(label.c_str(),
                                              selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput &&
                                                  selection_.targetId == output.id)) {
                            Select({
                                VfxWorkspaceSelectionKind::SpecialOutput,
                                output.id,
                                emitter->id,
                            });
                        }
                    }
                    ImGui::TreePop();
                }
            }
            DrawEmitterTree(asset, emitter->id);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

} // namespace HIKARI::EDITOR
