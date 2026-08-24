#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <optional>

#include "Editor/HIKARI_EditorContext.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {

void ApplyPresentationMutation(
    SequenceEditorDocument& document,
    bool editingEmbedded,
    std::optional<SceneCinematicsSettings> before,
    const SequencePresentationPanelResult& panelResult,
    CinematicsWorkspaceResult& workspaceResult) {
    if (!panelResult.sequenceChanged) {
        if (panelResult.editMergeId == 0 && !editingEmbedded) {
            document.SealMerge();
        }
        return;
    }
    if (editingEmbedded) {
        workspaceResult.cinematicsChanged = true;
        workspaceResult.timelineEditMergeId = panelResult.editMergeId;
    } else if (before) {
        document.RecordApplied(std::move(*before), panelResult.editMergeId);
    }
}

} // namespace

void CinematicsWorkspaceController::DrawPresentationTracksWindow(
    DocumentSceneBase& scene,
    EditorContext& context,
    CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
    const bool visible = ImGui::Begin(
        "Presentation Tracks###Cinematics/PresentationTracks", nullptr,
        ImGuiWindowFlags_NoCollapse);
    SequenceEditorDocument& document =
        sequenceDocumentController_.GetDocument();
    SceneCinematicsSettings& settings =
        document.GetSettings(scene.GetSceneDocument().cinematics);
    CinematicSequence* sequence =
        cameraTimelinePanel_.GetActiveSequence(settings);
    const bool editingEmbedded = document.IsEmbeddedScene();
    const bool editingAllowed = !scene.IsRuntimePlayActive();

    if (visible) {
        if (sequence == nullptr) {
            ImGui::TextDisabled(
                "Open or create a sequence to author presentation tracks.");
        } else {
            std::optional<SceneCinematicsSettings> before{};
            if (!editingEmbedded && editingAllowed) before = settings;
            SEQUENCER::SequenceBindingContext& previewBindings =
                cameraTimelinePanel_.GetPreviewBindings();
            const SequencePresentationPanelResult panelResult =
                sequencePresentationPanel_.DrawTimeline(
                    scene, *sequence, document.GetAssetGuid(), previewBindings,
                    context.selection.GetActiveObjectId(),
                    cameraTimelinePanel_.GetTimeSeconds(),
                    cameraTimelinePanel_.IsPlaying(), editingAllowed,
                    !editingEmbedded);
            if (panelResult.requestedPlayheadSeconds) {
                (void)cameraTimelinePanel_.Seek(
                    settings, *panelResult.requestedPlayheadSeconds);
            }
            ApplyPresentationMutation(
                document, editingEmbedded, std::move(before), panelResult,
                result);
        }
    }
    ImGui::End();

    if (sequence != nullptr && !scene.IsRuntimePlayActive()) {
        sequencePresentationPanel_.ApplyActorPreview(
            scene.GetWorld(), *sequence,
            cameraTimelinePanel_.GetPreviewBindings(),
            cameraTimelinePanel_.GetTimeSeconds());
    } else {
        sequencePresentationPanel_.RestoreActorPreview(scene.GetWorld());
    }
#else
    (void)scene;
    (void)context;
    (void)result;
#endif
}

void CinematicsWorkspaceController::DrawSequenceInspectorWindow(
    DocumentSceneBase& scene,
    CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(
            "Sequence Inspector###Cinematics/SequenceInspector", nullptr,
            ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    SequenceEditorDocument& document =
        sequenceDocumentController_.GetDocument();
    SceneCinematicsSettings& settings =
        document.GetSettings(scene.GetSceneDocument().cinematics);
    CinematicSequence* sequence =
        cameraTimelinePanel_.GetActiveSequence(settings);
    if (sequence == nullptr) {
        ImGui::TextDisabled("No active sequence.");
        ImGui::End();
        return;
    }
    const bool editingEmbedded = document.IsEmbeddedScene();
    const bool editingAllowed = !scene.IsRuntimePlayActive();
    std::optional<SceneCinematicsSettings> before{};
    if (!editingEmbedded && editingAllowed) before = settings;
    const SequencePresentationPanelResult panelResult =
        sequencePresentationPanel_.DrawInspector(
            scene.GetAssetDatabase(), *sequence, editingAllowed);
    ApplyPresentationMutation(
        document, editingEmbedded, std::move(before), panelResult, result);
    ImGui::End();
#else
    (void)scene;
    (void)result;
#endif
}

} // namespace HIKARI::EDITOR
