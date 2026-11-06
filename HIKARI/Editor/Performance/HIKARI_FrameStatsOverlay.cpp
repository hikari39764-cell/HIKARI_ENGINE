#include "Editor/Performance/HIKARI_FrameStatsOverlay.h"

#include <string>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Editor/Performance/HIKARI_MetricText.h"
#include "Editor/Style/HIKARI_EditorTheme.h"

namespace HIKARI::EDITOR {

void DrawFrameStatsOverlay(const ImVec2& anchor, const ImVec2& pivot) {
    const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(TIME::GetFrameContext().frameIndex);
    const auto value = [&metrics](const char* id, int decimals) {
        return MetricText(METRIC::FindReading(metrics, id), decimals);
    };
    const std::string line = "FPS " + value(METRIC::kFrameFps, 1) + "   CPU " + value(METRIC::kFrameCpuMs, 2) +
                             " ms   GPU " + value(METRIC::kFrameGpuMs, 2) + " ms";
    // 編集器では主菜单の行の右端に収まる高さにする。
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const ImVec2 text = ImGui::CalcTextSize(line.c_str());
    const ImVec2 size{text.x + padding.x * 2.0f, text.y + padding.y * 2.0f};
    const ImVec2 min{anchor.x - size.x * pivot.x, anchor.y - size.y * pivot.y};
    const ImVec2 max{min.x + size.x, min.y + size.y};
    const EditorThemePalette& palette = GetEditorThemePalette();
    ImDrawList* draw = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    draw->AddRectFilled(min, max, IM_COL32(0, 0, 0, 170), ImGui::GetStyle().WindowRounding);
    draw->AddText({min.x + padding.x, min.y + padding.y}, ImGui::GetColorU32(palette.text), line.c_str());
}

} // namespace HIKARI::EDITOR
