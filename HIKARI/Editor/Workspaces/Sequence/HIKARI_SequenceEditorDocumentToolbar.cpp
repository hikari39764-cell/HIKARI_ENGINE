#include "Editor/Workspaces/Sequence/HIKARI_SequenceEditorDocumentToolbar.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

SequenceEditorDocumentToolbarResult SequenceEditorDocumentToolbar::Draw(
    SequenceEditorDocumentController &controller, AssetDatabase &assetDatabase,
    const CinematicSequence *activeSequence,
    const std::string &sceneDisplayName, bool libraryVisible,
    bool actionsAllowed) const {

  SequenceEditorDocumentToolbarResult result{};
#if defined(HIKARI_WITH_EDITOR)
  SequenceEditorDocument &document = controller.GetDocument();
  if (IconToggleButton(EditorGlyph::Library, "SequenceLibraryToggle",
                       libraryVisible, ImVec2(28.0f, 28.0f),
                       libraryVisible ? "Hide sequence library"
                                      : "Show sequence library")) {
    result.toggleLibraryRequested = true;
  }
  ImGui::SameLine();
  const char *sourceLabel = "Embedded Scene";
  if (document.GetSourceKind() == SequenceEditorSourceKind::Asset) {
    sourceLabel = "Sequence Asset";
  } else if (document.GetSourceKind() ==
             SequenceEditorSourceKind::TransientAsset) {
    sourceLabel = "Unsaved Sequence Asset";
  }
  std::string documentName = activeSequence != nullptr
                                 ? activeSequence->name
                                 : document.GetDisplayName();
  if (document.IsDirty()) {
    documentName += " *";
  }
  ToolbarLabel(documentName.c_str());
  ImGui::SameLine();
  StatusBadge(sourceLabel, document.IsDirty() ? EditorStatusTone::Warning
                                              : EditorStatusTone::Normal);

  if (!actionsAllowed) {
    ImGui::BeginDisabled();
  }
  if (IconTextButton(EditorGlyph::NewDocument, "New", "SequenceDocumentNew",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                     "Create a new sequence asset")) {
    (void)controller.RequestNewAsset(assetDatabase, result.statusMessage);
  }
  ImGui::SameLine();
  if (activeSequence == nullptr) {
    ImGui::BeginDisabled();
  }
  if (IconTextButton(EditorGlyph::Save, "Save", "SequenceDocumentSave",
                     document.IsDirty() ? EditorButtonTone::Primary
                                        : EditorButtonTone::Neutral,
                     ImVec2(0.0f, 28.0f), "Save the active sequence")) {
    if (document.IsEmbeddedScene()) {
      result.saveEmbeddedSceneRequested = true;
    } else {
      (void)controller.Save(assetDatabase, result.statusMessage);
    }
  }
  ImGui::SameLine();
  if (IconButton(EditorGlyph::SaveAs, "SequenceDocumentSaveAs",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Save the active sequence as a new asset")) {
    (void)controller.SaveAs(assetDatabase, *activeSequence,
                            result.statusMessage);
  }
  if (activeSequence == nullptr) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  const bool canRevert =
      document.GetSourceKind() == SequenceEditorSourceKind::Asset;
  if (!canRevert) {
    ImGui::BeginDisabled();
  }
  if (IconButton(EditorGlyph::Revert, "SequenceDocumentRevert",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Revert the active sequence to its saved version")) {
    (void)controller.RequestRevert(assetDatabase, result.statusMessage);
  }
  if (!canRevert) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (!document.GetAssetGuid().IsValid()) {
    ImGui::BeginDisabled();
  }
  if (IconButton(EditorGlyph::Reveal, "SequenceDocumentReveal",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Reveal this sequence in the Resource Workspace")) {
    result.revealAssetRequested = true;
    result.revealAssetGuid = document.GetAssetGuid();
    result.revealAssetPath = document.GetSourcePath();
    result.statusMessage = "Sequence selected in Assets";
  }
  if (!document.GetAssetGuid().IsValid()) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (!document.IsExternalDocument()) {
    ImGui::BeginDisabled();
  }
  if (IconButton(EditorGlyph::Close, "SequenceDocumentClose",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Close the external sequence and return to the embedded scene "
                 "sequence")) {
    (void)controller.RequestCloseToEmbedded(assetDatabase,
                                            result.statusMessage);
  }
  if (!document.IsExternalDocument()) {
    ImGui::EndDisabled();
  }
  if (!actionsAllowed) {
    ImGui::EndDisabled();
  }

  if (document.GetSourceKind() == SequenceEditorSourceKind::TransientAsset) {
    StatusText("Save to create a reusable sequence asset.",
               EditorStatusTone::Warning);
  }
#else
  (void)controller;
  (void)assetDatabase;
  (void)activeSequence;
  (void)sceneDisplayName;
  (void)libraryVisible;
  (void)actionsAllowed;
#endif
  return result;
}

void SequenceEditorDocumentToolbar::DrawPendingConfirmation(
    SequenceEditorDocumentController &controller, AssetDatabase &assetDatabase,
    std::string &inOutStatusMessage) const {

#if defined(HIKARI_WITH_EDITOR)
  if (controller.HasPendingConfirmation() &&
      !ImGui::IsPopupOpen("Unsaved Sequence Asset")) {
    ImGui::OpenPopup("Unsaved Sequence Asset");
  }
  if (!ImGui::BeginPopupModal("Unsaved Sequence Asset", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("%s", controller.GetPendingConfirmationText());
  ImGui::Separator();
  if (IconTextButton(EditorGlyph::Save, "Save", "SequenceConfirmSave",
                     EditorButtonTone::Primary, ImVec2(108.0f, 30.0f))) {
    if (controller.ConfirmSaveThenContinue(assetDatabase, inOutStatusMessage)) {
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (IconTextButton(EditorGlyph::Delete, "Discard", "SequenceConfirmDiscard",
                     EditorButtonTone::Danger, ImVec2(108.0f, 30.0f))) {
    (void)controller.ConfirmDiscardAndContinue(assetDatabase,
                                               inOutStatusMessage);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (IconTextButton(EditorGlyph::Close, "Cancel", "SequenceConfirmCancel",
                     EditorButtonTone::Neutral, ImVec2(108.0f, 30.0f))) {
    controller.CancelPending();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
#else
  (void)controller;
  (void)assetDatabase;
  (void)inOutStatusMessage;
#endif
}

} // namespace HIKARI::EDITOR
