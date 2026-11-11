#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"

#include <cmath>
#include <string>

#include "Editor/NodeGraph/HIKARI_NodeGraphViewInternal.h"

// 節点図の描画：格子、線、節、進行中の線引きと枠選択。
namespace HIKARI::EDITOR {

using namespace EDITOR_PROJECT;
using namespace NODE_GRAPH_VIEW;

namespace {

// 強調なしは 0。
ImU32 HighlightColor(EditorNodeGraphHighlight highlight) noexcept {
    switch (highlight) {
    case EditorNodeGraphHighlight::Active:
    case EditorNodeGraphHighlight::Waiting: return IM_COL32(255, 214, 92, 255);
    case EditorNodeGraphHighlight::Success: return IM_COL32(104, 204, 120, 255);
    case EditorNodeGraphHighlight::Failure: return IM_COL32(232, 96, 88, 255);
    case EditorNodeGraphHighlight::Warning:
    case EditorNodeGraphHighlight::Aborted: return IM_COL32(240, 160, 64, 255);
    case EditorNodeGraphHighlight::None: break;
    }
    return 0u;
}

ImU32 WithAlpha(ImU32 color, float strength) noexcept {
    const auto alpha = static_cast<uint32_t>(std::clamp(strength, 0.0f, 1.0f) * 255.0f);
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

// 題の左の印。center は字の真ん中、size は字の高さ。
void DrawIcon(ImDrawList& drawList, EditorNodeGraphIcon icon, const ImVec2& center, float size, ImU32 color) {
    const float half = size * 0.36f;
    const float thickness = (std::max)(1.0f, size * 0.09f);
    switch (icon) {
    case EditorNodeGraphIcon::Composite:
        for (const float offset : {-0.66f, 0.0f, 0.66f}) {
            drawList.AddLine({center.x - half, center.y + half * offset}, {center.x + half, center.y + half * offset},
                             color, thickness);
        }
        break;
    case EditorNodeGraphIcon::Decorator:
        drawList.AddQuad({center.x, center.y - half}, {center.x + half, center.y}, {center.x, center.y + half},
                         {center.x - half, center.y}, color, thickness);
        break;
    case EditorNodeGraphIcon::Action:
        drawList.AddTriangleFilled({center.x - half * 0.7f, center.y - half}, {center.x + half, center.y},
                                   {center.x - half * 0.7f, center.y + half}, color);
        break;
    case EditorNodeGraphIcon::Condition:
        drawList.AddCircle(center, half, color, 16, thickness);
        drawList.AddCircleFilled(center, half * 0.35f, color);
        break;
    case EditorNodeGraphIcon::Subtree:
        drawList.AddRect({center.x - half, center.y - half}, {center.x + half * 0.4f, center.y + half * 0.4f}, color,
                         0.0f, 0, thickness);
        drawList.AddRect({center.x - half * 0.4f, center.y - half * 0.4f}, {center.x + half, center.y + half}, color,
                         0.0f, 0, thickness);
        break;
    case EditorNodeGraphIcon::None: break;
    }
}

// 折れ線の上を流れる点（今通っている道）。
void DrawFlow(ImDrawList& drawList, const ImVec2* points, int count, ImU32 color, float radius) {
    constexpr float kSpacing = 22.0f;
    const float offset = std::fmod(static_cast<float>(ImGui::GetTime()) * 40.0f, kSpacing);
    float travelled = 0.0f;
    for (int index = 0; index + 1 < count; ++index) {
        const ImVec2 from = points[index];
        const ImVec2 to = points[index + 1];
        const float length = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
        for (float at = std::fmod(kSpacing - std::fmod(travelled, kSpacing) + offset, kSpacing); at < length;
             at += kSpacing) {
            const float t = at / length;
            drawList.AddCircleFilled({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, radius, color);
        }
        travelled += length;
    }
}

void DrawGraphText(ImDrawList& drawList, const ImVec2& position, float textSize, ImU32 color, std::string_view text) {
    if (!text.empty()) {
        drawList.AddText(ImGui::GetFont(), textSize, position, color, text.data(), text.data() + text.size());
    }
}

} // namespace

void NodeGraphView::PaintNode(const EditorNodeGraphDesc& desc, const Frame& frame, uint32_t index) const {
    const EditorNodeGraphNode& node = desc.nodes[index];
    const NodeLayout& layout = nodes_[index];
    const ImVec2 minimum = ToIm(NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, layout.position));
    const ImVec2 maximum = ToIm(NodeGraphCanvas::CanvasToScreen(
        canvas_, frame.origin, {layout.position.x + layout.size.x, layout.position.y + layout.size.y}));
    if (maximum.x < frame.origin.x || minimum.x > frame.origin.x + frame.size.x || maximum.y < frame.origin.y ||
        minimum.y > frame.origin.y + frame.size.y) {
        return;
    }
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const float zoom = canvas_.zoom;
    const float fontSize = ImGui::GetFontSize() * zoom;
    const float smallSize = fontSize * kSmallText;
    const bool detailed = zoom >= kDetailZoom;
    const float rounding = kRounding * zoom;

    drawList.AddRectFilled(minimum, maximum, IM_COL32(29, 35, 43, 248), rounding);
    drawList.AddRectFilled(minimum, {maximum.x, minimum.y + layout.headerHeight * zoom}, ToColor(node.color), rounding,
                           ImDrawFlags_RoundCornersTop);
    if (const ImU32 highlight = HighlightColor(node.highlight); highlight != 0u && node.highlightStrength > 0.0f) {
        const bool waiting = node.highlight == EditorNodeGraphHighlight::Waiting;
        drawList.AddRect({minimum.x - 2.0f, minimum.y - 2.0f}, {maximum.x + 2.0f, maximum.y + 2.0f},
                         WithAlpha(highlight, node.highlightStrength * (waiting ? 0.75f : 1.0f)), rounding + 2.0f, 0,
                         waiting ? 1.5f : 3.0f);
    }
    drawList.AddRect(minimum, maximum, layout.selected ? IM_COL32(255, 171, 72, 255) : IM_COL32(69, 82, 96, 255),
                     rounding, 0, layout.selected ? 2.0f : 1.0f);
    float titleLeft = minimum.x + kPadX * zoom;
    const bool collapsed = HasFlag(node.flags, EditorNodeGraphNodeFlag::Collapsed);
    if (HasFlag(node.flags, EditorNodeGraphNodeFlag::Collapsible)) {
        // 題の左の三角：開いている時は下向き、畳んである時は右向き。
        const float side = fontSize * 0.62f;
        const ImVec2 center{titleLeft + fontSize * 0.5f, minimum.y + (kHeaderPadY * zoom) + fontSize * 0.5f};
        const ImU32 color = IM_COL32(244, 247, 250, 220);
        if (collapsed) {
            drawList.AddTriangleFilled({center.x - side * 0.4f, center.y - side * 0.5f},
                                       {center.x + side * 0.5f, center.y},
                                       {center.x - side * 0.4f, center.y + side * 0.5f}, color);
        } else {
            drawList.AddTriangleFilled({center.x - side * 0.5f, center.y - side * 0.4f},
                                       {center.x + side * 0.5f, center.y - side * 0.4f},
                                       {center.x, center.y + side * 0.5f}, color);
        }
        titleLeft += fontSize + kCollapseGap * zoom;
    }
    if (node.icon != EditorNodeGraphIcon::None) {
        DrawIcon(drawList, node.icon, {titleLeft + fontSize * 0.5f, minimum.y + kHeaderPadY * zoom + fontSize * 0.5f},
                 fontSize, IM_COL32(244, 247, 250, 235));
        titleLeft += fontSize + kCollapseGap * zoom;
    }
    // 題は角標と問題の印の手前で切る。
    float titleRight = maximum.x - kPadX * zoom;
    if (detailed && !View(node.badge).empty()) {
        titleRight -= (MeasureText(View(node.badge), kSmallText).x + kPadX) * zoom;
    }
    if (node.problem != EditorNodeGraphProblem::None) {
        titleRight -= fontSize;
    }
    if (renamingNodeId_ != node.id) {
        DrawGraphText(drawList, {titleLeft, minimum.y + kHeaderPadY * zoom}, fontSize, IM_COL32(244, 247, 250, 255),
                      FitText(View(node.title), (titleRight - titleLeft) / zoom));
    }
    if (node.problem != EditorNodeGraphProblem::None) {
        // 右上の角の問題の印（実行の強調と重ならない）。
        const ImVec2 center{maximum.x - 1.0f * zoom, minimum.y - 1.0f * zoom};
        const float radius = kOrderRadius * zoom;
        drawList.AddCircleFilled(center, radius,
                                 node.problem == EditorNodeGraphProblem::Error ? IM_COL32(222, 74, 66, 255)
                                                                               : IM_COL32(236, 160, 48, 255));
        drawList.AddCircle(center, radius, IM_COL32(15, 18, 23, 255));
        drawList.AddLine({center.x, center.y - radius * 0.55f}, {center.x, center.y + radius * 0.15f},
                         IM_COL32(255, 255, 255, 255), (std::max)(1.0f, 2.0f * zoom));
        drawList.AddCircleFilled({center.x, center.y + radius * 0.5f}, (std::max)(1.0f, 1.2f * zoom),
                                 IM_COL32(255, 255, 255, 255));
    }
    if (collapsed && node.hiddenCount != 0u) {
        // 畳んだ節の下に、隠れている節の数。
        const std::string hidden = "+" + std::to_string(node.hiddenCount);
        const float width = MeasureText(hidden, kSmallText).x * zoom;
        const ImVec2 textMin{(minimum.x + maximum.x - width) * 0.5f, maximum.y + 9.0f * zoom};
        drawList.AddRectFilled({textMin.x - 5.0f * zoom, textMin.y - 2.0f * zoom},
                               {textMin.x + width + 5.0f * zoom, textMin.y + smallSize + 2.0f * zoom},
                               IM_COL32(52, 62, 74, 255), smallSize);
        DrawGraphText(drawList, textMin, smallSize, IM_COL32(214, 222, 230, 255), hidden);
    }
    if (node.order != 0u) {
        // 兄弟の中の順番は、節の左上の外の丸。
        const std::string order = std::to_string(node.order);
        const ImVec2 center{minimum.x - 2.0f * zoom, minimum.y - 2.0f * zoom};
        drawList.AddCircleFilled(center, kOrderRadius * zoom, IM_COL32(60, 72, 86, 255));
        drawList.AddCircle(center, kOrderRadius * zoom, IM_COL32(15, 18, 23, 255));
        const float width = MeasureText(order, kSmallText).x * zoom;
        DrawGraphText(drawList, {center.x - width * 0.5f, center.y - smallSize * 0.5f}, smallSize,
                      IM_COL32(232, 238, 244, 255), order);
    }
    if (!detailed) {
        return;
    }
    if (const std::string_view badge = View(node.badge); !badge.empty()) {
        const float badgeWidth = MeasureText(badge, kSmallText).x * zoom;
        DrawGraphText(drawList, {maximum.x - kPadX * zoom - badgeWidth, minimum.y + (kHeaderPadY + 1.0f) * zoom},
                      smallSize, IM_COL32(244, 247, 250, 190), badge);
    }
    PaintNodeBody(node, layout, minimum, maximum);
    if (HasFlag(node.flags, EditorNodeGraphNodeFlag::Disabled)) {
        // 止めた節は灰色に沈める。
        drawList.AddRectFilled(minimum, maximum, IM_COL32(15, 18, 23, 150), rounding);
    }
    for (uint32_t pinIndex = 0u; pinIndex < node.inputCount + node.outputCount; ++pinIndex) {
        const PinLayout& pin = pins_[layout.firstPin + pinIndex];
        const std::string_view label = View(pin.pin->label);
        if (label.empty()) {
            continue;
        }
        const ImVec2 center = ToIm(NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, pin.position));
        const ImU32 labelColor = IM_COL32(204, 213, 222, 255);
        if (desc.flow == EditorNodeGraphFlow::TopToBottom) {
            const float top = minimum.y + (pin.output ? layout.outputLabelTop : layout.inputLabelTop) * zoom;
            DrawGraphText(drawList, {center.x - MeasureText(label, kSmallText).x * zoom * 0.5f, top}, smallSize,
                          labelColor, label);
        } else if (pin.output) {
            DrawGraphText(drawList, {center.x - kPadX * zoom - MeasureText(label).x * zoom, center.y - fontSize * 0.5f},
                          fontSize, labelColor, label);
        } else {
            DrawGraphText(drawList, {center.x + kPadX * zoom, center.y - fontSize * 0.5f}, fontSize, labelColor, label);
        }
    }
}

void NodeGraphView::PaintNodeBody(const EditorNodeGraphNode& node, const NodeLayout& layout, const ImVec2& minimum,
                                  const ImVec2& maximum) const {
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const float zoom = canvas_.zoom;
    const float smallSize = ImGui::GetFontSize() * kSmallText * zoom;
    const float step = (ImGui::GetFontSize() * kSmallText + 1.0f) * zoom;
    const float left = minimum.x + kPadX * zoom;
    // 本体の字が使える幅（図の座標）。
    const float room = (maximum.x - minimum.x) / zoom - kPadX * 2.0f;
    float lineTop = minimum.y + layout.detailTop * zoom;
    if (const std::string_view subtitle = View(node.subtitle); !subtitle.empty()) {
        DrawGraphText(drawList, {left, lineTop}, smallSize, IM_COL32(150, 164, 180, 255),
                      FitText(subtitle, room, kSmallText));
        lineTop += step;
    }
    ForEachLine(View(node.detail), [&](std::string_view line) {
        DrawGraphText(drawList, {left, lineTop}, smallSize, IM_COL32(170, 184, 198, 255),
                      FitText(line, room, kSmallText));
        lineTop += step;
    });
    for (uint32_t row = 0u; row < node.rowCount; ++row) {
        const EditorNodeGraphRow& item = node.rows[row];
        // 値を先に（半分まで）取り、名前は残りの幅に収める。
        const float pad = item.capsule != 0u ? kCapsulePadX : 0.0f;
        const std::string value = FitText(View(item.value), room * 0.5f - pad * 2.0f, kSmallText);
        const std::string label = FitText(
            View(item.label), room - MeasureText(value, kSmallText).x - pad * 2.0f - kLabelGap * 0.5f, kSmallText);
        DrawGraphText(drawList, {left, lineTop}, smallSize, IM_COL32(150, 164, 180, 255), label);
        const float valueWidth = MeasureText(value, kSmallText).x * zoom;
        const float valueLeft = maximum.x - kPadX * zoom - valueWidth - pad * zoom;
        if (item.capsule != 0u) {
            drawList.AddRectFilled({valueLeft - pad * zoom, lineTop - 1.0f * zoom},
                                   {valueLeft + valueWidth + pad * zoom, lineTop + smallSize + 1.0f * zoom},
                                   IM_COL32(48, 74, 104, 255), smallSize);
        }
        DrawGraphText(drawList, {valueLeft, lineTop}, smallSize,
                      item.capsule != 0u ? IM_COL32(214, 232, 250, 255) : IM_COL32(220, 228, 236, 255), value);
        lineTop += step + (item.capsule != 0u ? 2.0f * zoom : 0.0f);
    }
    if (node.progress >= 0.0f) {
        const float barTop = lineTop + 2.0f * zoom;
        const float barRight = maximum.x - kPadX * zoom;
        drawList.AddRectFilled({left, barTop}, {barRight, barTop + kProgressBarHeight * zoom},
                               IM_COL32(44, 52, 62, 255), kProgressBarHeight * zoom);
        drawList.AddRectFilled(
            {left, barTop},
            {left + (barRight - left) * std::clamp(node.progress, 0.0f, 1.0f), barTop + kProgressBarHeight * zoom},
            IM_COL32(255, 214, 92, 255), kProgressBarHeight * zoom);
        lineTop = barTop + (kProgressBarHeight + 1.0f) * zoom;
        DrawGraphText(drawList, {left, lineTop}, smallSize, IM_COL32(236, 224, 180, 255),
                      FitText(View(node.progressText), room, kSmallText));
        lineTop += step;
    }
    uint32_t noteLine = 0u;
    ForEachLine(View(node.note), [&](std::string_view line) {
        if (noteLine == 0u) {
            lineTop += kBodyPad * 0.5f * zoom;
            drawList.AddLine({left, lineTop - kBodyPad * 0.25f * zoom},
                             {maximum.x - kPadX * zoom, lineTop - kBodyPad * 0.25f * zoom}, IM_COL32(58, 68, 80, 255));
        }
        if (noteLine++ < kNoteLines) {
            DrawGraphText(drawList, {left, lineTop}, smallSize, IM_COL32(196, 188, 160, 255),
                          FitText(line, room, kSmallText));
            lineTop += step;
        }
    });
}

void NodeGraphView::PaintDropHint(const EditorNodeGraphDesc& desc, const Frame& frame) const {
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImU32 color = desc.dropBlocked != 0u ? IM_COL32(232, 96, 88, 255) : IM_COL32(104, 204, 120, 255);
    const auto toScreen = [&](const EditorWorkspacePoint& point) {
        return ToIm(NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, {point.x, point.y}));
    };
    for (uint32_t index = 0u; desc.dropTargetNodeId != 0u && index < desc.nodeCount; ++index) {
        if (desc.nodes[index].id == desc.dropTargetNodeId) {
            const NodeLayout& node = nodes_[index];
            const ImVec2 minimum = ToIm(NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, node.position));
            const ImVec2 maximum = ToIm(NodeGraphCanvas::CanvasToScreen(
                canvas_, frame.origin, {node.position.x + node.size.x, node.position.y + node.size.y}));
            drawList.AddRect({minimum.x - 4.0f, minimum.y - 4.0f}, {maximum.x + 4.0f, maximum.y + 4.0f}, color,
                             kRounding * canvas_.zoom + 4.0f, 0, 3.0f);
        }
    }
    if (desc.dropLineFrom.x != desc.dropLineTo.x || desc.dropLineFrom.y != desc.dropLineTo.y) {
        const ImVec2 from = toScreen(desc.dropLineFrom);
        const ImVec2 to = toScreen(desc.dropLineTo);
        drawList.AddLine(from, to, color, 4.0f);
        drawList.AddCircleFilled(from, 5.0f, color);
        drawList.AddCircleFilled(to, 5.0f, color);
    }
    if (const std::string_view message = View(desc.dropMessage); !message.empty()) {
        const ImVec2 pointer = ImGui::GetIO().MousePos;
        const MATH::Vec2 size = MeasureText(message);
        const ImVec2 textMin{pointer.x + 18.0f, pointer.y + 18.0f};
        drawList.AddRectFilled({textMin.x - 6.0f, textMin.y - 4.0f},
                               {textMin.x + size.x + 6.0f, textMin.y + size.y + 4.0f}, IM_COL32(24, 28, 34, 240), 4.0f);
        drawList.AddRect({textMin.x - 6.0f, textMin.y - 4.0f}, {textMin.x + size.x + 6.0f, textMin.y + size.y + 4.0f},
                         color, 4.0f);
        DrawGraphText(drawList, textMin, ImGui::GetFontSize(), IM_COL32(236, 240, 244, 255), message);
    }
}

void NodeGraphView::Paint(const EditorNodeGraphDesc& desc, const Frame& frame) const {
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ToIm(frame.origin);
    const ImVec2 canvasMax{origin.x + frame.size.x, origin.y + frame.size.y};
    const float zoom = canvas_.zoom;
    drawList.AddRectFilled(origin, canvasMax, IM_COL32(15, 18, 23, 255));
    if (const float gridStep = 32.0f * zoom; gridStep >= 10.0f) {
        for (float x = std::fmod(canvas_.pan.x, gridStep); x < frame.size.x; x += gridStep) {
            drawList.AddLine({origin.x + x, origin.y}, {origin.x + x, canvasMax.y}, IM_COL32(35, 40, 48, 160));
        }
        for (float y = std::fmod(canvas_.pan.y, gridStep); y < frame.size.y; y += gridStep) {
            drawList.AddLine({origin.x, origin.y + y}, {canvasMax.x, origin.y + y}, IM_COL32(35, 40, 48, 160));
        }
    }
    const auto toScreen = [&](const MATH::Vec2& point) {
        return ToIm(NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, point));
    };
    const auto drawCurve = [&](const std::array<MATH::Vec2, 4>& curve, ImU32 color, float thickness) {
        drawList.AddBezierCubic(toScreen(curve[0]), toScreen(curve[1]), toScreen(curve[2]), toScreen(curve[3]), color,
                                thickness);
    };
    // 縮めても線と引脚が消えないよう、太さと半径には下限を置く。
    const float strokeScale = (std::max)(zoom, 0.6f);

    for (uint32_t index = 0u; index < desc.linkCount; ++index) {
        const EditorNodeGraphLink& link = desc.links[index];
        const PinLayout* output = FindPin(link.outputPinId);
        const PinLayout* input = FindPin(link.inputPinId);
        if (output == nullptr || input == nullptr) {
            continue;
        }
        const ImU32 highlight = HighlightColor(link.highlight);
        const bool selected = Contains(selectedLinks_, link.id);
        const ImU32 color = selected                                ? IM_COL32(255, 190, 78, 255)
                            : highlight != 0u                       ? highlight
                            : static_cast<int>(index) == frame.link ? IM_COL32(225, 235, 242, 255)
                                                                    : ToColor(link.color);
        const float thickness = (selected || highlight != 0u ? 3.0f : 2.0f) * strokeScale;
        if (desc.linkStyle == EditorNodeGraphLinkStyle::Elbow) {
            const std::array<MATH::Vec2, 4> elbow = LinkElbow(output->position, input->position);
            const std::array<ImVec2, 4> points{toScreen(elbow[0]), toScreen(elbow[1]), toScreen(elbow[2]),
                                               toScreen(elbow[3])};
            const float width = link.flowing != 0u ? thickness * 1.6f : thickness;
            if (std::fabs(points[0].x - points[3].x) < 0.5f) {
                // 真下の子：折れ点が一直線に重なると線の継ぎ目が崩れるので、一本で引く。
                drawList.AddLine(points[0], points[3], color, width);
            } else {
                drawList.AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, width);
            }
            if (link.flowing != 0u) {
                DrawFlow(drawList, points.data(), static_cast<int>(points.size()), IM_COL32(255, 244, 200, 255),
                         width * 0.9f);
            }
        } else {
            drawCurve(LinkCurve(desc.flow, output->position, input->position), color, thickness);
        }
    }

    // 選んだ節、拖動中の節の順に上に描く。引脚は節の縁に載るので、節の後で描く。
    for (const int pass : {0, 1, 2}) {
        for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
            const NodeLayout& node = nodes_[index];
            if ((node.dragged ? 2 : node.selected ? 1 : 0) == pass) {
                PaintNode(desc, frame, index);
            }
        }
    }
    for (size_t index = 0u; index < pins_.size(); ++index) {
        const ImVec2 center = toScreen(pins_[index].position);
        const bool hot = frame.pin == static_cast<int>(index);
        drawList.AddCircleFilled(center, (hot ? kPinRadius + 1.5f : kPinRadius) * strokeScale,
                                 hot ? IM_COL32(255, 255, 255, 255) : ToColor(pins_[index].pin->color));
        drawList.AddCircle(center, kPinRadius * strokeScale, IM_COL32(15, 18, 23, 255));
    }

    if (action_ == Action::DragLink) {
        if (const PinLayout* from = FindPin(dragPinId_)) {
            drawCurve(from->output ? LinkCurve(desc.flow, from->position, frame.pointer)
                                   : LinkCurve(desc.flow, frame.pointer, from->position),
                      IM_COL32(255, 190, 78, 255), 2.0f);
        }
    } else if (action_ == Action::MoveNodes && desc.dragDescendants != 0u) {
        PaintDropHint(desc, frame);
    } else if (action_ == Action::BoxSelect) {
        const ImVec2 start = toScreen(boxStart_);
        const ImVec2 end = toScreen(frame.pointer);
        drawList.AddRectFilled(start, end, IM_COL32(255, 190, 78, 28));
        drawList.AddRect(start, end, IM_COL32(255, 190, 78, 200));
    }
}

} // namespace HIKARI::EDITOR
