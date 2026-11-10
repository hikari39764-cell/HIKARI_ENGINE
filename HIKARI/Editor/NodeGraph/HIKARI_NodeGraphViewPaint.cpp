#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"

#include <cmath>

#include "Editor/NodeGraph/HIKARI_NodeGraphViewInternal.h"

// 節点図の描画：格子、線、節、進行中の線引きと枠選択。
namespace HIKARI::EDITOR {

using namespace EDITOR_PROJECT;
using namespace NODE_GRAPH_VIEW;

namespace {

// 強調なしは 0。
ImU32 HighlightColor(EditorNodeGraphHighlight highlight) noexcept {
    switch (highlight) {
    case EditorNodeGraphHighlight::Active: return IM_COL32(255, 214, 92, 255);
    case EditorNodeGraphHighlight::Success: return IM_COL32(104, 204, 120, 255);
    case EditorNodeGraphHighlight::Failure: return IM_COL32(232, 96, 88, 255);
    case EditorNodeGraphHighlight::Warning: return IM_COL32(240, 160, 64, 255);
    case EditorNodeGraphHighlight::None: break;
    }
    return 0u;
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
    if (const ImU32 highlight = HighlightColor(node.highlight); highlight != 0u) {
        drawList.AddRect({minimum.x - 2.0f, minimum.y - 2.0f}, {maximum.x + 2.0f, maximum.y + 2.0f}, highlight,
                         rounding + 2.0f, 0, 3.0f);
    }
    drawList.AddRect(minimum, maximum, layout.selected ? IM_COL32(255, 171, 72, 255) : IM_COL32(69, 82, 96, 255),
                     rounding, 0, layout.selected ? 2.0f : 1.0f);
    DrawGraphText(drawList, {minimum.x + kPadX * zoom, minimum.y + kHeaderPadY * zoom}, fontSize,
                  IM_COL32(244, 247, 250, 255), View(node.title));
    if (!detailed) {
        return;
    }
    if (const std::string_view badge = View(node.badge); !badge.empty()) {
        const float badgeWidth = MeasureText(badge, kSmallText).x * zoom;
        DrawGraphText(drawList, {maximum.x - kPadX * zoom - badgeWidth, minimum.y + (kHeaderPadY + 1.0f) * zoom},
                      smallSize, IM_COL32(244, 247, 250, 190), badge);
    }
    float lineTop = minimum.y + layout.detailTop * zoom;
    ForEachLine(View(node.detail), [&](std::string_view line) {
        DrawGraphText(drawList, {minimum.x + kPadX * zoom, lineTop}, smallSize, IM_COL32(170, 184, 198, 255), line);
        lineTop += (ImGui::GetFontSize() * kSmallText + 1.0f) * zoom;
    });
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
        drawCurve(LinkCurve(desc.flow, output->position, input->position), color,
                  (selected || highlight != 0u ? 3.0f : 2.0f) * strokeScale);
    }

    // 選んだ節を上に描く。引脚は節の縁に載るので、節の後で描く。
    for (const bool selected : {false, true}) {
        for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
            if (nodes_[index].selected == selected) {
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
    } else if (action_ == Action::BoxSelect) {
        const ImVec2 start = toScreen(boxStart_);
        const ImVec2 end = toScreen(frame.pointer);
        drawList.AddRectFilled(start, end, IM_COL32(255, 190, 78, 28));
        drawList.AddRect(start, end, IM_COL32(255, 190, 78, 200));
    }
}

} // namespace HIKARI::EDITOR
