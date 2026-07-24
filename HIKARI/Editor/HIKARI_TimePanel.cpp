#include "HIKARI_TimePanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void TimePanel::DrawContents() const {
#if defined(HIKARI_WITH_EDITOR)
        const FrameContext& frame = TIME::GetFrameContext();

        ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(frame.frameIndex));
        ImGui::Separator();

        ImGui::Text("rawDt:      %.6f", frame.rawDt);
        ImGui::Text("unscaledDt: %.6f", frame.unscaledDt);
        ImGui::Text("gameDt:     %.6f", frame.gameDt);
        ImGui::Text("fixedDt:    %.6f", frame.fixedDt);
        ImGui::Text(
            "fixedTick:  %llu",
            static_cast<unsigned long long>(frame.fixedTickIndex));
        ImGui::Text("fixedSteps: %u", frame.fixedStepsThisFrame);
        ImGui::Text("fixedAlpha: %.3f", frame.fixedInterpolationAlpha);
        if (frame.droppedFixedTime > 0.0f) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "dropped fixed time: %.4f s",
                frame.droppedFixedTime);
        }
        ImGui::Separator();

        ImGui::Text("timeScale:  %.3f", frame.gameTimeScale);
        ImGui::Text("paused:     %s", frame.paused ? "true" : "false");

        ImGui::Separator();
        ImGui::Text("FPS(raw):   %.1f", frame.rawDt > 0.0f ? (1.0f / frame.rawDt) : 0.0f);
        ImGui::Text("FPS(game):  %.1f", frame.gameDt > 0.0f ? (1.0f / frame.gameDt) : 0.0f);

        ImGui::Separator();
        ImGui::TextUnformatted("Quick Controls");

        if (ImGui::Button("Pause / Resume")) {
            TIME::SetPaused(!TIME::IsPaused());
        }
        ImGui::SameLine();
        if (ImGui::Button("TimeScale 1.0")) {
            TIME::SetGameTimeScale(1.0f);
        }

        if (ImGui::Button("TimeScale 0.1")) {
            TIME::SetGameTimeScale(0.1f);
        }
        ImGui::SameLine();
        if (ImGui::Button("TimeScale 2.0")) {
            TIME::SetGameTimeScale(2.0f);
        }

        const RenderSubmissionDebugStats& renderStats = RenderSubmissionSystem::GetDebugStats();

        ImGui::Separator();
        ImGui::TextUnformatted("Render Submission System");
        ImGui::Text("Scanned Models:  %d", renderStats.scannedModelCount);
        ImGui::Text("Submitted Models: %d", renderStats.submittedModelCount);
        ImGui::Text("Hidden Models:   %d", renderStats.hiddenModelCount);
        ImGui::Text("Culled Models:   %d", renderStats.culledModelCount);
        ImGui::Text("Missing Bounds:  %d", renderStats.missingBoundsCount);
        ImGui::Text("Skinned Safe Draw: %d", renderStats.skinnedCullSkippedCount);
        ImGui::Text("Fallback Wires:   %d", renderStats.fallbackWireCount);
#endif
    }

} // namespace HIKARI
