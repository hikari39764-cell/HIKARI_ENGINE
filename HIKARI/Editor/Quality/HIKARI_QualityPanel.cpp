#include "Editor/Quality/HIKARI_QualityPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Quality/HIKARI_QualityPanelSections.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

#include <string>

#include "imgui.h"

namespace HIKARI {

namespace {
void DrawTransitionDebug() {
    const SceneTransitionBus* transitionBus = RuntimeSceneContext::GetTransitionBus();
    if (!transitionBus) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.transitionBusUnavailable").c_str());
        return;
    }

    const TransitionVisualState visualState = transitionBus->GetVisualState();
    const SceneTransitionBus::TransitionState state = transitionBus->GetState();
    std::string stateLabel = TEXT::Localize("ui.quality.transitionUnknown");
    switch (state) {
    case SceneTransitionBus::TransitionState::Idle: stateLabel = TEXT::Localize("ui.quality.transitionIdle"); break;
    case SceneTransitionBus::TransitionState::TransitionOut:
        stateLabel = TEXT::Localize("ui.quality.transitionOut");
        break;
    case SceneTransitionBus::TransitionState::SwitchingScene:
        stateLabel = TEXT::Localize("ui.quality.transitionSwitching");
        break;
    case SceneTransitionBus::TransitionState::TransitionIn:
        stateLabel = TEXT::Localize("ui.quality.transitionIn");
        break;
    default: break;
    }

    ImGui::Text(TEXT::Localize("ui.quality.profileIdFormat").c_str(), visualState.profileId.empty()
                                                                          ? TEXT::Localize("ui.common.none").c_str()
                                                                          : visualState.profileId.c_str());
    ImGui::Text(TEXT::Localize("ui.quality.stateFormat").c_str(), stateLabel.c_str());
    ImGui::Text(TEXT::Localize("ui.quality.progressFormat").c_str(), visualState.progress);
    ImGui::Text(TEXT::Localize("ui.quality.outDurationFormat").c_str(), visualState.outDuration);
    ImGui::Text(TEXT::Localize("ui.quality.inDurationFormat").c_str(), visualState.inDuration);
}
} // namespace

bool QualityPanel::Draw() {
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.quality", "Quality").c_str())) {
        ImGui::End();
        return false;
    }
    // 画面の見た目（露出・泛光・色調）は環境プロファイルが持つ。以前この窓にあったので行き先を示す。
    ImGui::TextDisabled("%s", TEXT::Localize("ui.quality.lookMovedHint").c_str());
    const bool renderQualityChanged = EDITOR::QUALITY_PANEL::DrawRenderSettings();
    if (ImGui::TreeNode(TEXT::Localize("ui.quality.transitionDebug").c_str())) {
        DrawTransitionDebug();
        ImGui::TreePop();
    }
    ImGui::End();
    return renderQualityChanged;
}

} // namespace HIKARI
