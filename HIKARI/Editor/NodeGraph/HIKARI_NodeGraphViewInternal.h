#pragma once

#include <algorithm>
#include <cfloat>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "HIKARI/Project/EditorNodeGraph.h"

// NodeGraphView の三つの実装ファイル（操作・配置・描画）が共有する寸法と小さい道具。
namespace HIKARI::EDITOR::NODE_GRAPH_VIEW {

// 寸法は図の座標（拡縮 1 の時の画素）。
inline constexpr float kPadX = 10.0f;
inline constexpr float kHeaderPadY = 5.0f;
inline constexpr float kRowGap = 4.0f;
inline constexpr float kBodyPad = 6.0f;
inline constexpr float kPinRadius = 5.0f;
inline constexpr float kPinHitRadius = 10.0f;
inline constexpr float kMinNodeWidth = 120.0f;
inline constexpr float kRounding = 6.0f;
inline constexpr float kSmallText = 0.88f;
inline constexpr float kLabelGap = 16.0f;
// これより縮めたら、節は題だけを描く。
inline constexpr float kDetailZoom = 0.55f;
// これより動かさずに離したら、拖動ではなくクリック（画面の画素）。
inline constexpr float kClickSlop = 4.0f;

inline ImVec2 ToIm(const MATH::Vec2& value) noexcept { return {value.x, value.y}; }

inline ImU32 ToColor(const EDITOR_PROJECT::EditorWorkspaceColor& color) noexcept {
    return ImGui::ColorConvertFloat4ToU32(ImVec4{std::clamp(color.r, 0.0f, 1.0f), std::clamp(color.g, 0.0f, 1.0f),
                                                 std::clamp(color.b, 0.0f, 1.0f), std::clamp(color.a, 0.0f, 1.0f)});
}

inline std::string_view View(const char* text) noexcept {
    return text != nullptr ? std::string_view(text) : std::string_view{};
}

// 今の書体での字の大きさ（図の座標）。
inline MATH::Vec2 MeasureText(std::string_view text, float scale = 1.0f) {
    if (text.empty()) {
        return {};
    }
    const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0.0f, text.data(),
                                                        text.data() + text.size());
    return {size.x, size.y};
}

template <class Visit>
void ForEachLine(std::string_view text, Visit&& visit) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        visit(text.substr(0u, end));
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1u);
    }
}

inline bool Contains(const std::vector<uint64_t>& ids, uint64_t id) noexcept {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace HIKARI::EDITOR::NODE_GRAPH_VIEW
