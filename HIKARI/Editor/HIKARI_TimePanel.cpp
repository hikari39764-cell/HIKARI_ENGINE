#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_TimePanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "imgui.h"

namespace HIKARI {

void TimePanel::DrawContents() const {
    const FrameContext& frame = TIME::GetFrameContext();

    ImGui::Text(TEXT::Localize("editor.ui.frame.index.llu.85e39d").c_str(),
                static_cast<unsigned long long>(frame.frameIndex));
    ImGui::Separator();

    ImGui::Text(TEXT::Localize("editor.ui.rawdt.6f.6a752b").c_str(), frame.rawDt);
    ImGui::Text(TEXT::Localize("editor.ui.unscaleddt.6f.8f272c").c_str(), frame.unscaledDt);
    ImGui::Text(TEXT::Localize("editor.ui.gamedt.6f.87f49e").c_str(), frame.gameDt);
    ImGui::Text(TEXT::Localize("editor.ui.fixeddt.6f.0d092a").c_str(), frame.fixedDt);
    ImGui::Text(TEXT::Localize("editor.ui.fixedtick.llu.d1290e").c_str(),
                static_cast<unsigned long long>(frame.fixedTickIndex));
    ImGui::Text(TEXT::Localize("editor.ui.fixedsteps.u.7bcb25").c_str(), frame.fixedStepsThisFrame);
    ImGui::Text(TEXT::Localize("editor.ui.fixedalpha.3f.712504").c_str(), frame.fixedInterpolationAlpha);
    if (frame.droppedFixedTime > 0.0f) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                           TEXT::Localize("editor.ui.dropped.fixed.time.4f.s.b1352f").c_str(), frame.droppedFixedTime);
    }
    ImGui::Separator();

    ImGui::Text(TEXT::Localize("editor.ui.timescale.3f.b31343").c_str(), frame.gameTimeScale);
    ImGui::Text(TEXT::Localize("editor.ui.paused.s.9579bd").c_str(), frame.paused ? "true" : "false");

    ImGui::Separator();
    ImGui::Text("FPS(raw):   %.1f", frame.rawDt > 0.0f ? (1.0f / frame.rawDt) : 0.0f);
    ImGui::Text("FPS(game):  %.1f", frame.gameDt > 0.0f ? (1.0f / frame.gameDt) : 0.0f);

    ImGui::Separator();
    ImGui::TextUnformatted(TEXT::Localize("ui.environment.quickControls").c_str());

    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.pause.resume.d1d0f7", "Pause / Resume").c_str())) {
        TIME::SetPaused(!TIME::IsPaused());
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.timescale.1.0.7cf52a", "TimeScale 1.0").c_str())) {
        TIME::SetGameTimeScale(1.0f);
    }

    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.timescale.0.1.2716be", "TimeScale 0.1").c_str())) {
        TIME::SetGameTimeScale(0.1f);
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.timescale.2.0.d15b9c", "TimeScale 2.0").c_str())) {
        TIME::SetGameTimeScale(2.0f);
    }

    const RenderSubmissionDebugStats& renderStats = RenderSubmissionSystem::GetDebugStats();

    ImGui::Separator();
    ImGui::TextUnformatted(TEXT::Localize("editor.ui.render.submission.system.5fa68c").c_str());
    ImGui::Text(TEXT::Localize("editor.ui.scanned.models.d.4809df").c_str(), renderStats.scannedModelCount);
    ImGui::Text(TEXT::Localize("editor.ui.submitted.models.d.697848").c_str(), renderStats.submittedModelCount);
    ImGui::Text(TEXT::Localize("editor.ui.hidden.models.d.5a9810").c_str(), renderStats.hiddenModelCount);
    ImGui::Text(TEXT::Localize("editor.ui.culled.models.d.3e1505").c_str(), renderStats.culledModelCount);
    ImGui::Text(TEXT::Localize("editor.ui.missing.bounds.d.3c8f02").c_str(), renderStats.missingBoundsCount);
    ImGui::Text(TEXT::Localize("editor.ui.skinned.safe.draw.d.ec2da9").c_str(), renderStats.skinnedCullSkippedCount);
    ImGui::Text(TEXT::Localize("editor.ui.fallback.wires.d.b6d968").c_str(), renderStats.fallbackWireCount);
}

} // namespace HIKARI
