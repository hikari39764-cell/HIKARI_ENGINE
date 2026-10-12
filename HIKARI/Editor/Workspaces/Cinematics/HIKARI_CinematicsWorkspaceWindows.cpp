#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_TimeService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
void CinematicsWorkspaceController::DrawDockSpace(bool reset) const {
#if defined(HIKARI_WITH_EDITOR)
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
        return;
    const auto* viewport = ImGui::GetMainViewport();
    // 二画面を既定配置に追加。安定 ID は言語を変更しても共通。
    const auto id = ImGui::GetID("HIKARI_CinematicsDockSpace_v10");
    if (reset || !ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id, timeline = 0, left = 0, right = 0, monitor = 0;
        // 时间轴は行が増えても読める高さ、左の镜头と素材は页签の名前が省略されない幅。
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &timeline, &center);
        const float scale = ImGui::GetFontSize() / 16.0f;
        const float width = (std::max)(1.0f, viewport->WorkSize.x);
        const float leftWidth = std::clamp(280.0f * scale, width * 0.13f, width * 0.22f);
        const float rightWidth = std::clamp(340.0f * scale, width * 0.17f, width * 0.26f);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftWidth / width, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightWidth / (width - leftWidth), &right, &center);
        // 空間视图と画面は、16:9 の画が大きく出る方の並べ方にする（縦長の中央に横に並べると上下が空く）。
        const float centerWidth = width - leftWidth - rightWidth;
        const float centerHeight = viewport->WorkSize.y * 0.70f;
        const float sideBySide = (std::min)(centerWidth * 0.5f, centerHeight * 16.0f / 9.0f);
        const float stacked = (std::min)(centerWidth, centerHeight * 0.5f * 16.0f / 9.0f);
        const bool stackViews = stacked > sideBySide;
        ImGui::DockBuilderSplitNode(center, stackViews ? ImGuiDir_Down : ImGuiDir_Right, 0.48f, &monitor, &center);
        ImGui::DockBuilderDockWindow("###Cinematics/Space", center);
        ImGui::DockBuilderDockWindow("素材与镜头###Cinematics/Resources", left);
        ImGui::DockBuilderDockWindow("画面###Cinematics/Monitor", monitor);
        ImGui::DockBuilderDockWindow("属性与效果###Cinematics/Inspector", right);
        ImGui::DockBuilderDockWindow("演出时间轴###Cinematics/Timeline", timeline);
        ImGui::DockBuilderFinish(id);
    }
    HIKARI::EDITOR::DrawWorkspaceDockSpace(id, viewport);
#endif
}

CinematicsWorkspaceResult CinematicsWorkspaceController::Draw(DocumentSceneBase& scene, EditorContext& context,
                                                              EditorWorkspaceHost& workspaceHost,
                                                              EditorOperationRegistry& operations) {
    CinematicsWorkspaceResult result;
    result.statusMessage.swap(pendingStatusMessage_);
#if defined(HIKARI_WITH_EDITOR)
    if (sequenceDocumentController_.ConsumeTimelineResetRequested())
        ResetEditing(scene);
    auto& document = sequenceDocumentController_.GetDocument();
    bool enabled = !scene.IsRuntimePlayActive();
    if (ImGui::Begin(TEXT::LocalizeWindow("cine.assetsShots", "Cinematics/Resources").c_str(), nullptr,
                     ImGuiWindowFlags_NoCollapse)) {
        auto toolbar =
            sequenceDocumentToolbar_.Draw(sequenceDocumentController_, scene.GetAssetDatabase(), &ActiveSequence(scene),
                                          scene.GetCurrentSceneDisplayName(), sequenceLibraryVisible_, enabled);
        result.saveSceneRequested = toolbar.saveEmbeddedSceneRequested;
        if (toolbar.toggleLibraryRequested)
            sequenceLibraryVisible_ = !sequenceLibraryVisible_;
        if (!toolbar.statusMessage.empty())
            result.statusMessage = toolbar.statusMessage;
        if (sequenceDocumentController_.ConsumeTimelineResetRequested())
            ResetEditing(scene);
        auto& settings = document.GetSettings(scene.GetSceneDocument().cinematics);
        if (settings.sequences.size() > 1 && ImGui::BeginCombo(TEXT::LocalizeWindow("cine.current", "当前演出").c_str(),
                                                               ActiveSequence(scene).name.c_str())) {
            for (const auto& item : settings.sequences) {
                ImGui::PushID(static_cast<int>(item.id.value));
                if (ImGui::Selectable(item.name.c_str(), item.id == activeSequenceId_)) {
                    ResetEditing(scene);
                    activeSequenceId_ = item.id;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (sequenceLibraryVisible_ &&
            ImGui::CollapsingHeader(TEXT::LocalizeWindow("cine.asset", "演出资产").c_str())) {
            auto library = sequenceLibraryPanel_.Draw(scene.GetAssetDatabase(), document.GetAssetGuid(), enabled);
            if (library.action == SequenceLibraryActionKind::OpenAsset)
                RequestOpenSequenceAsset(scene, library.assetGuid, result.statusMessage);
            if (library.action == SequenceLibraryActionKind::NewAsset)
                RequestNewSequenceAsset(scene, result.statusMessage);
        }
    }
    ImGui::End();
    auto before = document.GetSettings(scene.GetSceneDocument().cinematics);
    auto& sequence = ActiveSequence(scene);
    if (!cursor_.IsBound())
        cursor_.Bind(sequence.durationSeconds);
    cursor_.SetDuration(sequence.durationSeconds);
    if (autoPreview_ && enabled && !sequence.cameraCutTrack.clips.empty()) {
        previewEnabled_ = true;
        autoPreview_ = false;
    }
    bool changed = false;
    if (ImGui::Begin(TEXT::LocalizeWindow("cine.assetsShots", "Cinematics/Resources").c_str())) {
        ImGui::BeginDisabled(!enabled);
        changed |= DrawResources(scene, sequence, context, operations);
        ImGui::EndDisabled();
    }
    ImGui::End();
    if (ImGui::Begin(TEXT::LocalizeWindow("cine.timeline", "Cinematics/Timeline").c_str(), nullptr,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginDisabled(!enabled);
        changed |= DrawTransport(sequence);
        changed |= DrawTimeline(scene, sequence);
        ImGui::EndDisabled();
    }
    ImGui::End();
    if (ImGui::Begin(TEXT::LocalizeWindow("cine.inspector", "Cinematics/Inspector").c_str(), nullptr,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginDisabled(!enabled);
        changed |= DrawInspector(scene, sequence);
        ImGui::EndDisabled();
    }
    ImGui::End();
    DrawMonitor(scene, workspaceHost, result);
    changed |= DrawStage(scene, sequence, context, workspaceHost, enabled);
    if (changed) {
        NormalizeCinematicSequence(sequence);
        previewChanged_ = true;
        if (document.IsEmbeddedScene())
            result.cinematicsChanged = true;
        else
            document.RecordApplied(std::move(before), mergeId_);
    }
    // 飛行録画の間は右ボタンを離しても一つの取り消しにまとめる。
    if (ImGui::IsAnyItemActive() || flight_ == FlightState::Recording)
        result.timelineEditMergeId = mergeId_;
    else {
        document.SealMerge();
        ++mergeId_;
    }
    if (enabled && previewEnabled_) {
        auto evaluation = cursor_.Tick(ImGui::GetIO().DeltaTime, SEQUENCER::SequenceEvaluationMode::Preview);
        if (!scene.PreviewSequence(sequence, previewBindings_, evaluation, previewChanged_)) {
            pendingStatusMessage_ = TEXT::Localize("cine.previewNotReady");
            previewEnabled_ = false;
            if (ownsPreview_) {
                scene.EndSequencePreview();
                ownsPreview_ = false;
            }
        }
        ownsPreview_ = previewEnabled_;
        previewChanged_ = false;
    } else if (ownsPreview_) {
        scene.EndSequencePreview();
        ownsPreview_ = false;
    }
    if (!result.statusMessage.empty()) {
        if (ImGui::Begin(TEXT::LocalizeWindow("cine.timeline", "Cinematics/Timeline").c_str()))
            ImGui::TextWrapped("%s", result.statusMessage.c_str());
        ImGui::End();
    }
    sequenceDocumentToolbar_.DrawPendingConfirmation(sequenceDocumentController_, scene.GetAssetDatabase(),
                                                     result.statusMessage);
#endif
    return result;
}
} // namespace HIKARI::EDITOR
