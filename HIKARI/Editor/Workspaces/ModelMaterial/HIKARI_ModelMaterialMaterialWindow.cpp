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

const char *BindingLabel(ModelMaterialBindingSource source) noexcept {
  switch (source) {
  case ModelMaterialBindingSource::ImportedFallback:
    return "Imported draft";
  case ModelMaterialBindingSource::AuthoredOverride:
    return "Authored material";
  case ModelMaterialBindingSource::OverrideUnavailable:
    return "Override unavailable";
  }
  return "Unknown";
}

} // namespace

void ModelMaterialWorkspaceController::DrawMaterialWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Properties###ModelMaterial/Material")) {
    ImGui::End();
    return;
  }

  if (authoringMode_ == AuthoringMode::ModelMaterials) {
    const ModelMaterialSlotView *slot = GetSelectedSlot();
    if (slot == nullptr) {
      ImGui::TextWrapped(
          "Select a model part. Its imported or authored material will open "
          "here immediately.");
      DrawPreviewLightingProperties();
      ImGui::End();
      return;
    }
    ImGui::TextDisabled("EDITING MODEL PART");
    ImGui::TextWrapped("%s", slot->displayName.c_str());
    StatusBadge(BindingLabel(slot->source),
                slot->source == ModelMaterialBindingSource::AuthoredOverride
                    ? EditorStatusTone::Ready
                    : EditorStatusTone::Normal);
    ImGui::SameLine();
    ImGui::BeginDisabled(
        slot->source == ModelMaterialBindingSource::ImportedFallback &&
        !slot->overrideMaterialGuid.IsValid());
    if (ImGui::SmallButton("Restore Imported")) {
      (void)RestoreSelectedImportedMaterial(scene, statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "The cyan row and viewport outline identify the material part being edited.");

    if (ImGui::TreeNodeEx("Advanced Part Binding",
                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
      std::string replacementGuid = candidateMaterialGuid_.value;
      AssetFieldOptions options{};
      options.label = "Replace with Material";
      options.requiredType = AssetType::Material;
      if (DrawAssetField(&scene.GetAssetDatabase(), options,
                         replacementGuid)) {
        const AssetGuid replacement{replacementGuid};
        const AssetChoice *choice = FindMaterialChoice(replacement);
        if (choice != nullptr && choice->ready &&
            (choice->materialKind == MaterialAssetKind::Material ||
             choice->materialKind == MaterialAssetKind::Instance)) {
          ModelMaterialBindingCommand command{};
          command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
          command.slotId = slot->slotId;
          command.materialIndexHint = slot->materialIndex;
          command.materialAssetGuid = replacement;
          const ModelMaterialBindingCommandResult bound =
              ExecuteBindingCommand(command, scene.GetAssetDatabase(),
                                    scene.GetAssetRegistry());
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
    ImGui::TextDisabled("STANDALONE MATERIAL ASSET");
    ImGui::TextWrapped(
        "This document is previewed on the material sphere and is not tied "
        "to a model until you assign it.");
    ImGui::Separator();
  }

  if (!authoringDocument_.HasMaterial()) {
    ImGui::TextWrapped(
        authoringMode_ == AuthoringMode::MaterialAsset
            ? "Create or open a Material Asset from the left panel."
            : "Select a model part to edit its material.");
    DrawPreviewLightingProperties();
    ImGui::End();
    return;
  }

  MaterialAssetData &source = authoringDocument_.EditMaterial();
  ImGui::Text("%s%s", source.materialName.c_str(),
              authoringDocument_.IsMaterialDirty() ? " *" : "");
  ImGui::TextDisabled("%s | %s", ToString(source.kind).data(),
                      ToString(source.templateId).data());

  const MaterialCompileResult &compile =
      authoringDocument_.GetCompileResult();
  if (compile.success) {
    StatusBadge("Live preview", EditorStatusTone::Ready);
  } else {
    StatusBadge("Invalid v5 graph - preview paused", EditorStatusTone::Error);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Every committed control refreshes runtime bindings.");
  if (!materialPreviewExpressionStatus_.empty()) {
    ImGui::TextColored(
        ImVec4{ 0.55f, 0.82f, 1.0f, 1.0f },
        "%s", materialPreviewExpressionStatus_.c_str());
  }

  ImGui::BeginDisabled(!authoringDocument_.IsMaterialDirty() ||
                       !compile.success);
  if (ImGui::Button("Save")) {
    (void)SaveMaterial(scene, false, statusMessage_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Save As...")) {
    ImGui::OpenPopup("MaterialSaveAsPopup");
  }
  if (ImGui::BeginPopup("MaterialSaveAsPopup")) {
    ImGui::TextUnformatted("Path under Assets");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("##MaterialSaveAsPath", saveAsPath_.data(),
                     saveAsPath_.size());
    if (ImGui::Button("Save Copy") &&
        SaveMaterial(scene, true, statusMessage_)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (source.kind == MaterialAssetKind::Instance) {
    ImGui::SeparatorText("Material Instance");
    ImGui::TextWrapped(
        "Inherited values and overrides are edited in the Instance tab. "
        "The model part still previews the resolved runtime material live.");
  } else if (source.kind == MaterialAssetKind::Ramp) {
    ImGui::SeparatorText("Ramp Asset");
    ImGui::TextWrapped(
        "Edit the curve and colors in the Ramp tab, then select this Ramp "
        "from a Ramp Diffuse or Multi Ramp node.");
  } else if (source.kind == MaterialAssetKind::Profile) {
    ImGui::SeparatorText("Material Profile");
    ImGui::TextWrapped(
        "Profiles group a base template and reusable stylized resources.");
  }

  if (MaterialGraphData *graph =
          MATERIAL_GRAPH_UI::EditableGraph(source)) {
    ImGui::SeparatorText("Graph Selection");
    if (const MaterialGraphNodeData *node =
            MATERIAL_GRAPH_UI::FindNode(*graph, selectedGraphNodeId_)) {
      ImGui::TextWrapped("%s",
                         node->displayName.empty()
                             ? ToString(node->kind).data()
                             : node->displayName.c_str());
      ImGui::TextDisabled(
          "Edit values and resources directly on the node card.");
    } else if (!selectedGraphEdgeId_.empty()) {
      ImGui::TextWrapped("A graph connection is selected. Delete or rewire "
                         "it in the graph toolbar/canvas.");
    } else {
      ImGui::TextDisabled("Select a node or connection in the graph.");
    }
    if (ImGui::TreeNodeEx("Advanced Node Details",
                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
      if (DrawMaterialGraphInspector(scene, source, *graph) &&
          !selectedGraphEdgeId_.empty()) {
        MaterialAssetData before = source;
        const std::string deletedId = selectedGraphEdgeId_;
        std::erase_if(graph->edges, [&deletedId](const auto &edge) {
          return edge.id == deletedId;
        });
        selectedGraphEdgeId_.clear();
        CommitMaterialMutation(std::move(before),
                               "Delete material graph edge", scene);
      }
      ImGui::TreePop();
    }
  }

  DrawPreviewLightingProperties();

  if (ImGui::CollapsingHeader("Advanced Asset Actions")) {
    const AssetGuid materialGuid =
        authoringDocument_.GetMaterialAssetGuid();
    const AssetTrashReferenceAnalysis references =
        AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), materialGuid);
    ImGui::BeginDisabled(!materialGuid.IsValid() || references.IsReferenced());
    if (ImGui::Button("Move Material to Trash...",
                      ImVec2(-1.0f, 0.0f))) {
      ImGui::OpenPopup("DeleteCurrentMaterialPopup");
    }
    ImGui::EndDisabled();
    if (references.IsReferenced()) {
      ImGui::TextDisabled(
          "Restore or replace every reference before deleting this asset.");
    }
  }

  if (ImGui::BeginPopupModal("DeleteCurrentMaterialPopup", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const AssetGuid materialGuid =
        authoringDocument_.GetMaterialAssetGuid();
    const AssetRecord *record = materialGuid.IsValid()
                                    ? scene.GetAssetDatabase().FindByGuid(
                                          materialGuid)
                                    : nullptr;
    ImGui::TextUnformatted("Move this material to project trash?");
    if (record != nullptr) {
      ImGui::TextWrapped("%s",
                         record->sourcePath.generic_string().c_str());
    }
    const AssetTrashReferenceAnalysis references =
        AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), materialGuid);
    ImGui::BeginDisabled(references.IsReferenced());
    if (ImGui::Button("Move to Trash", ImVec2(120.0f, 0.0f))) {
      const AssetTrashResult deleted =
          MoveAssetToProjectTrash(scene.GetAssetDatabase(), materialGuid);
      statusMessage_ = deleted.message;
      if (deleted.success) {
        authoringDocument_.CloseMaterial();
        candidateAuthoringAssetGuid_ = {};
        candidateMaterialGuid_ = {};
        (void)scene.ReloadAssets();
        assetChoiceRevision_ = 0u;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
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
