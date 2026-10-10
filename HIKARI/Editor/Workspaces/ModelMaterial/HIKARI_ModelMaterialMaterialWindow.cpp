#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Assets/HIKARI_AssetTrashService.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

const char* BindingLabel(ModelMaterialBindingSource source) noexcept {
    switch (source) {
    case ModelMaterialBindingSource::ImportedFallback: return TEXT::Localize("editor.ui.imported.draft.a67748").c_str();
    case ModelMaterialBindingSource::AuthoredOverride:
        return TEXT::Localize("editor.ui.authored.material.fde952").c_str();
    case ModelMaterialBindingSource::OverrideUnavailable:
        return TEXT::Localize("editor.ui.override.unavailable.2787dc").c_str();
    }
    return TEXT::Localize("ui.quality.transitionUnknown").c_str();
}

} // namespace

void ModelMaterialWorkspaceController::DrawMaterialWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.properties.bc6c88", "ModelMaterial/Material").c_str())) {
        ImGui::End();
        return;
    }

    if (authoringMode_ == AuthoringMode::ModelMaterials) {
        const ModelMaterialSlotView* slot = GetSelectedSlot();
        if (slot == nullptr) {
            ImGui::TextWrapped(
                TEXT::Localize("editor.ui.select.a.model.part.its.imported.or.authored.material.will.open..4b6905")
                    .c_str());
            DrawPreviewLightingProperties();
            ImGui::End();
            return;
        }
        ImGui::TextDisabled(TEXT::Localize("editor.ui.editing.model.part.bc0abd").c_str());
        ImGui::TextWrapped("%s", slot->displayName.c_str());
        StatusBadge(BindingLabel(slot->source), slot->source == ModelMaterialBindingSource::AuthoredOverride
                                                    ? EditorStatusTone::Ready
                                                    : EditorStatusTone::Normal);
        ImGui::SameLine();
        ImGui::BeginDisabled(slot->source == ModelMaterialBindingSource::ImportedFallback &&
                             !slot->overrideMaterialGuid.IsValid());
        if (ImGui::SmallButton(
                TEXT::LocalizeWindow("ui.inspector.model.restoreImported", "Restore Imported").c_str())) {
            (void)RestoreSelectedImportedMaterial(scene, statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.the.cyan.row.and.viewport.outline.identify.the.material.part.bei.011578")
                .c_str());
        if (authoringDocument_.HasMaterial() &&
            ImGui::Button(TEXT::Localize("material.painterly.createForPart").c_str(), ImVec2(-1.0f, 0.0f))) {
            (void)CreateMaterial(scene, MaterialTemplateId::PainterlyShell, false, statusMessage_);
            ImGui::End();
            return;
        }

        if (ImGui::TreeNodeEx(
                TEXT::LocalizeWindow("editor.ui.advanced.part.binding.f0dacd", "Advanced Part Binding").c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth)) {
            std::string replacementGuid = candidateMaterialGuid_.value;
            AssetFieldOptions options{};
            options.label = "Replace with Material";
            options.requiredType = AssetType::Material;
            if (DrawAssetField(&scene.GetAssetDatabase(), options, replacementGuid)) {
                const AssetGuid replacement{replacementGuid};
                const AssetChoice* choice = FindMaterialChoice(replacement);
                if (choice != nullptr && choice->ready &&
                    (choice->materialKind == MaterialAssetKind::Material ||
                     choice->materialKind == MaterialAssetKind::Instance)) {
                    ModelMaterialBindingCommand command{};
                    command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
                    command.slotId = slot->slotId;
                    command.materialIndexHint = slot->materialIndex;
                    command.materialAssetGuid = replacement;
                    const ModelMaterialBindingCommandResult bound =
                        ExecuteBindingCommand(command, scene.GetAssetDatabase(), scene.GetAssetRegistry());
                    statusMessage_ = bound.message;
                    if (bound.accepted) {
                        candidateMaterialGuid_ = replacement;
                        (void)OpenMaterialAsset(scene, replacement, statusMessage_);
                    }
                }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
    } else {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.standalone.material.asset.876261").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.this.document.is.previewed.on.the.material.sphere.and.is.not.tie.2400d3")
                .c_str());
        ImGui::Separator();
    }

    if (!authoringDocument_.HasMaterial()) {
        ImGui::TextWrapped(
            authoringMode_ == AuthoringMode::MaterialAsset
                ? TEXT::Localize("editor.ui.create.or.open.a.material.asset.from.the.left.panel.266612").c_str()
                : TEXT::Localize("editor.ui.select.a.model.part.to.edit.its.material.3a7ca7").c_str());
        DrawPreviewLightingProperties();
        ImGui::End();
        return;
    }

    MaterialAssetData& source = authoringDocument_.EditMaterial();
    ImGui::Text("%s%s", source.materialName.c_str(), authoringDocument_.IsMaterialDirty() ? " *" : "");
    ImGui::TextDisabled("%s | %s", ToString(source.kind).data(), ToString(source.templateId).data());

    const MaterialCompileResult& compile = authoringDocument_.GetCompileResult();
    if (compile.success) {
        StatusBadge(TEXT::Localize("editor.ui.live.preview.131a90").c_str(), EditorStatusTone::Ready);
    } else {
        StatusBadge(TEXT::Localize("editor.ui.invalid.v5.graph.preview.paused.b42e13").c_str(),
                    EditorStatusTone::Error);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(TEXT::Localize("editor.ui.every.committed.control.refreshes.runtime.bindings.f10e6d").c_str());
    if (!materialPreviewExpressionStatus_.empty()) {
        ImGui::TextColored(ImVec4{0.55f, 0.82f, 1.0f, 1.0f}, "%s", materialPreviewExpressionStatus_.c_str());
    }

    ImGui::BeginDisabled(!authoringDocument_.IsMaterialDirty() || !compile.success);
    if (ImGui::Button(TEXT::LocalizeWindow("ui.common.save", "Save").c_str())) {
        (void)SaveMaterial(scene, false, statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.as.d89163", "Save As...").c_str())) {
        ImGui::OpenPopup("MaterialSaveAsPopup");
    }
    if (ImGui::BeginPopup("MaterialSaveAsPopup")) {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.path.under.assets.9905f5").c_str());
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("##MaterialSaveAsPath", saveAsPath_.data(), saveAsPath_.size());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.copy.ec185a", "Save Copy").c_str()) &&
            SaveMaterial(scene, true, statusMessage_)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (source.kind == MaterialAssetKind::Instance) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.material.instance.27dfb2").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.inherited.values.and.overrides.are.edited.in.the.instance.tab.th.44eb6a")
                .c_str());
    } else if (source.kind == MaterialAssetKind::Ramp) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.ramp.asset.5fb9d4").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.edit.the.curve.and.colors.in.the.ramp.tab.then.select.this.ramp..8a7644")
                .c_str());
    } else if (source.kind == MaterialAssetKind::Profile) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.material.profile.3353df").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.profiles.group.a.base.template.and.reusable.stylized.resources.f4a3e0").c_str());
    }

    if (MaterialGraphData* graph = MATERIAL_GRAPH_UI::EditableGraph(source)) {
        if (source.kind == MaterialAssetKind::Material) {
            ImGui::SeparatorText(TEXT::Localize("material.parameters.quickControls").c_str());
            if (source.templateId == MaterialTemplateId::PainterlyShell)
                ImGui::TextWrapped("%s", TEXT::Localize("material.painterly.workflow").c_str());
            DrawMaterialParameterControls(source, *graph);
        }
        if (DrawMaterialGraphInspector(scene, source, *graph) && !selectedGraphEdgeId_.empty()) {
            (void)DeleteSelectedGraphEdge(scene, statusMessage_);
        }
    }

    DrawPreviewLightingProperties();

    if (ImGui::CollapsingHeader(
            TEXT::LocalizeWindow("editor.ui.advanced.asset.actions.e5ed94", "Advanced Asset Actions").c_str())) {
        const AssetGuid materialGuid = authoringDocument_.GetMaterialAssetGuid();
        const AssetTrashReferenceAnalysis references =
            AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), materialGuid);
        ImGui::BeginDisabled(!materialGuid.IsValid() || references.IsReferenced());
        if (ImGui::Button(
                TEXT::LocalizeWindow("editor.ui.move.material.to.trash.ca24ab", "Move Material to Trash...").c_str(),
                ImVec2(-1.0f, 0.0f))) {
            ImGui::OpenPopup(
                TEXT::LocalizeWindow("editor.ui.deletecurrentmaterialpopup.6d5ce9", "DeleteCurrentMaterialPopup")
                    .c_str());
        }
        ImGui::EndDisabled();
        if (references.IsReferenced()) {
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.restore.or.replace.every.reference.before.deleting.this.asset.1b8e0b")
                    .c_str());
        }
    }

    if (ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.deletecurrentmaterialpopup.6d5ce9", "DeleteCurrentMaterialPopup").c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const AssetGuid materialGuid = authoringDocument_.GetMaterialAssetGuid();
        const AssetRecord* record =
            materialGuid.IsValid() ? scene.GetAssetDatabase().FindByGuid(materialGuid) : nullptr;
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.move.this.material.to.project.trash.e366e7").c_str());
        if (record != nullptr) {
            ImGui::TextWrapped("%s", record->sourcePath.generic_string().c_str());
        }
        const AssetTrashReferenceAnalysis references =
            AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), materialGuid);
        ImGui::BeginDisabled(references.IsReferenced());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.move.to.trash.dffd3b", "Move to Trash").c_str(),
                          ImVec2(120.0f, 0.0f))) {
            const AssetTrashResult deleted = MoveAssetToProjectTrash(scene.GetAssetDatabase(), materialGuid);
            statusMessage_ = deleted.message;
            if (deleted.success) {
                authoringDocument_.CloseMaterial();
                candidateAuthoringAssetGuid_ = {};
                candidateMaterialGuid_ = {};
                if (!scene.ReloadAssets()) {
                    statusMessage_ += "; Registry publication failed";
                } else {
                    assetChoiceRevision_ = 0u;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
#else
    (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
