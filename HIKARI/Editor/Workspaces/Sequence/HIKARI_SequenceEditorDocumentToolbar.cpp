#include "Editor/Workspaces/Sequence/HIKARI_SequenceEditorDocumentToolbar.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

SequenceEditorDocumentToolbarResult SequenceEditorDocumentToolbar::Draw(
    SequenceEditorDocumentController& controller, AssetDatabase& assetDatabase, const CinematicSequence* activeSequence,
    const std::string& sceneDisplayName, bool libraryVisible, bool actionsAllowed) const {
    SequenceEditorDocumentToolbarResult result{};
    (void)sceneDisplayName;
    SequenceEditorDocument& document = controller.GetDocument();
    if (IconToggleButton(EditorGlyph::Library, "SequenceLibraryToggle", libraryVisible, ImVec2(28.0f, 28.0f),
                         (libraryVisible ? TEXT::Localize("ui.sequence.toolbar.hideLibrary")
                                         : TEXT::Localize("ui.sequence.toolbar.showLibrary"))
                             .c_str())) {
        result.toggleLibraryRequested = true;
    }
    NextToolbarItem(28);
    std::string sourceLabel = TEXT::Localize("ui.sequence.toolbar.sourceEmbedded");
    if (document.GetSourceKind() == SequenceEditorSourceKind::Asset) {
        sourceLabel = TEXT::Localize("ui.sequence.toolbar.sourceAsset");
    } else if (document.GetSourceKind() == SequenceEditorSourceKind::TransientAsset) {
        sourceLabel = TEXT::Localize("ui.sequence.toolbar.sourceTransient");
    }
    std::string documentName = activeSequence != nullptr ? activeSequence->name : document.GetDisplayName();
    if (document.IsDirty()) {
        documentName += " *";
    }
    ImGui::TextWrapped("%s", documentName.c_str());
    NextToolbarItem(ImGui::CalcTextSize(sourceLabel.c_str()).x + 20);
    StatusBadge(sourceLabel.c_str(), document.IsDirty() ? EditorStatusTone::Warning : EditorStatusTone::Normal);

    if (!actionsAllowed) {
        ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::NewDocument, "SequenceDocumentNew", EditorButtonTone::Neutral, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.newTooltip").c_str())) {
        (void)controller.RequestNewAsset(assetDatabase, result.statusMessage);
    }
    NextToolbarItem(28);
    if (activeSequence == nullptr) {
        ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Save, "SequenceDocumentSave",
                   document.IsDirty() ? EditorButtonTone::Primary : EditorButtonTone::Neutral, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.saveTooltip").c_str())) {
        if (document.IsEmbeddedScene()) {
            result.saveEmbeddedSceneRequested = true;
        } else {
            (void)controller.Save(assetDatabase, result.statusMessage);
        }
    }
    NextToolbarItem(28);
    if (IconButton(EditorGlyph::SaveAs, "SequenceDocumentSaveAs", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.saveAsTooltip").c_str())) {
        (void)controller.SaveAs(assetDatabase, *activeSequence, result.statusMessage);
    }
    if (activeSequence == nullptr) {
        ImGui::EndDisabled();
    }

    NextToolbarItem(28);
    const bool canRevert = document.GetSourceKind() == SequenceEditorSourceKind::Asset;
    if (!canRevert) {
        ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Revert, "SequenceDocumentRevert", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.revertTooltip").c_str())) {
        (void)controller.RequestRevert(assetDatabase, result.statusMessage);
    }
    if (!canRevert) {
        ImGui::EndDisabled();
    }

    NextToolbarItem(28);
    if (!document.GetAssetGuid().IsValid()) {
        ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Reveal, "SequenceDocumentReveal", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.revealTooltip").c_str())) {
        result.revealAssetRequested = true;
        result.revealAssetGuid = document.GetAssetGuid();
        result.revealAssetPath = document.GetSourcePath();
        result.statusMessage = TEXT::Localize("ui.sequence.toolbar.revealed");
    }
    if (!document.GetAssetGuid().IsValid()) {
        ImGui::EndDisabled();
    }

    NextToolbarItem(28);
    if (!document.IsExternalDocument()) {
        ImGui::BeginDisabled();
    }
    if (IconButton(EditorGlyph::Close, "SequenceDocumentClose", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("ui.sequence.toolbar.closeTooltip").c_str())) {
        (void)controller.RequestCloseToEmbedded(assetDatabase, result.statusMessage);
    }
    if (!document.IsExternalDocument()) {
        ImGui::EndDisabled();
    }
    if (!actionsAllowed) {
        ImGui::EndDisabled();
    }

    if (document.GetSourceKind() == SequenceEditorSourceKind::TransientAsset) {
        StatusText(TEXT::Localize("ui.sequence.toolbar.transientHint").c_str(), EditorStatusTone::Warning);
    }
    return result;
}

void SequenceEditorDocumentToolbar::DrawPendingConfirmation(SequenceEditorDocumentController& controller,
                                                            AssetDatabase& assetDatabase,
                                                            std::string& inOutStatusMessage) const {
    const std::string unsavedTitle =
        TEXT::LocalizeWindow("ui.sequence.toolbar.sourceTransient", "Unsaved Sequence Asset");
    if (controller.HasPendingConfirmation() && !ImGui::IsPopupOpen(unsavedTitle.c_str())) {
        ImGui::OpenPopup(unsavedTitle.c_str());
    }
    if (!ImGui::BeginPopupModal(unsavedTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("%s", controller.GetPendingConfirmationText());
    ImGui::Separator();
    if (IconTextButton(EditorGlyph::Save, TEXT::Localize("ui.sequence.toolbar.save").c_str(), "SequenceConfirmSave",
                       EditorButtonTone::Primary, ImVec2(108.0f, 30.0f))) {
        if (controller.ConfirmSaveThenContinue(assetDatabase, inOutStatusMessage)) {
            ImGui::CloseCurrentPopup();
        }
    }
    NextToolbarItem(28);
    if (IconTextButton(EditorGlyph::Delete, TEXT::Localize("ui.sequence.toolbar.discard").c_str(),
                       "SequenceConfirmDiscard", EditorButtonTone::Danger, ImVec2(108.0f, 30.0f))) {
        (void)controller.ConfirmDiscardAndContinue(assetDatabase, inOutStatusMessage);
        ImGui::CloseCurrentPopup();
    }
    NextToolbarItem(28);
    if (IconTextButton(EditorGlyph::Close, TEXT::Localize("ui.sequence.toolbar.cancel").c_str(),
                       "SequenceConfirmCancel", EditorButtonTone::Neutral, ImVec2(108.0f, 30.0f))) {
        controller.CancelPending();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace HIKARI::EDITOR
