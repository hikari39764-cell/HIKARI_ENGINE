#pragma once

#include <algorithm>
#include <cfloat>
#include <string>
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
// 縮められる下限（大きい樹の全体が収まるまで）。
inline constexpr float kMinZoom = 0.2f;
// これより縮めたら、節は題だけを描く。
inline constexpr float kDetailZoom = 0.55f;
// これより動かさずに離したら、拖動ではなくクリック（画面の画素）。
inline constexpr float kClickSlop = 4.0f;
// 題の左の畳む三角の幅（字の高さに対する比）と、題との間。
inline constexpr float kCollapseGap = 4.0f;
// 節の左上の外に出す順番の丸の半径。
inline constexpr float kOrderRadius = 9.0f;
// 直角の線が出力から下りてから横へ折れるまでの長さの下限。
inline constexpr float kElbowDrop = 14.0f;

// 進み具合の棒の高さと、値の札の左右の余白。
inline constexpr float kProgressBarHeight = 5.0f;
inline constexpr float kCapsulePadX = 5.0f;
// 注は二行まで。
inline constexpr uint32_t kNoteLines = 2u;

inline bool HasFlag(uint32_t flags, EDITOR_PROJECT::EditorNodeGraphNodeFlag flag) noexcept {
    return (flags & static_cast<uint32_t>(flag)) != 0u;
}

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

// maxWidth（図の座標）に収まらない字を「…」で切る。
inline std::string FitText(std::string_view text, float maxWidth, float scale = 1.0f) {
    if (text.empty() || MeasureText(text, scale).x <= maxWidth) {
        return std::string(text);
    }
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    const float budget = maxWidth - MeasureText(kEllipsis, scale).x;
    size_t end = text.size();
    while (end > 0u) {
        // UTF-8 の文字の頭まで戻る。
        do {
            --end;
        } while (end > 0u && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u);
        if (MeasureText(text.substr(0u, end), scale).x <= budget) {
            break;
        }
    }
    return std::string(text.substr(0u, end)) + std::string(kEllipsis);
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
