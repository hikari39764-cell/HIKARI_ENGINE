#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"

#include <cmath>

#include "Editor/NodeGraph/HIKARI_NodeGraphViewInternal.h"

// 節点図の配置（節の大きさ、引脚の位置）と当たり判定。
namespace HIKARI::EDITOR {

using namespace EDITOR_PROJECT;
using namespace NODE_GRAPH_VIEW;

namespace {

bool HasLabel(const EditorNodeGraphPin* pins, uint32_t count) noexcept {
    for (uint32_t index = 0u; index < count; ++index) {
        if (pins[index].label != nullptr && pins[index].label[0] != '\0') {
            return true;
        }
    }
    return false;
}

// 樹の向きで、引脚の名前を一行に並べた時の幅。
float LabelRowWidth(const EditorNodeGraphPin* pins, uint32_t count) {
    float total = 0.0f;
    for (uint32_t pin = 0u; pin < count; ++pin) {
        total += MeasureText(View(pins[pin].label), kSmallText).x + kLabelGap;
    }
    return total;
}

float WidestLabel(const EditorNodeGraphPin* pins, uint32_t count) {
    float widest = 0.0f;
    for (uint32_t pin = 0u; pin < count; ++pin) {
        widest = (std::max)(widest, MeasureText(View(pins[pin].label)).x);
    }
    return widest;
}

} // namespace

void NodeGraphView::BuildLayout(const EditorNodeGraphDesc& desc) {
    nodes_.clear();
    pins_.clear();
    pinById_.clear();
    sizes_.clear();
    const float lineHeight = ImGui::GetFontSize();
    const float smallLine = lineHeight * kSmallText + 1.0f;
    const float rowHeight = lineHeight + kRowGap;
    const bool tree = desc.flow == EditorNodeGraphFlow::TopToBottom;
    for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
        const EditorNodeGraphNode& node = desc.nodes[index];
        NodeLayout layout{};
        layout.selected = Contains(selectedNodes_, node.id);
        layout.position = {node.position.x, node.position.y};
        if (layout.selected && action_ == Action::MoveNodes) {
            layout.position.x += moveOffset_.x;
            layout.position.y += moveOffset_.y;
        }
        layout.firstPin = static_cast<uint32_t>(pins_.size());
        layout.headerHeight = lineHeight + kHeaderPadY * 2.0f;

        float width = MeasureText(View(node.title)).x;
        if (const std::string_view badge = View(node.badge); !badge.empty()) {
            width += kPadX + MeasureText(badge, kSmallText).x;
        }
        float detailHeight = 0.0f;
        ForEachLine(View(node.detail), [&](std::string_view line) {
            width = (std::max)(width, MeasureText(line, kSmallText).x);
            detailHeight += smallLine;
        });
        if (detailHeight > 0.0f) {
            detailHeight += kBodyPad;
        }

        float body = layout.headerHeight;
        if (tree) {
            // 親の引脚は上の辺、子の引脚は下の辺。名前がある時だけ、辺の内側に一行取る。
            if (HasLabel(node.inputs, node.inputCount)) {
                width = (std::max)(width, LabelRowWidth(node.inputs, node.inputCount));
                layout.inputLabelTop = body + 2.0f;
                body += smallLine + 2.0f;
            }
            layout.detailTop = body + kBodyPad * 0.5f;
            body += detailHeight;
            if (HasLabel(node.outputs, node.outputCount)) {
                width = (std::max)(width, LabelRowWidth(node.outputs, node.outputCount));
                layout.outputLabelTop = body + 2.0f;
                body += smallLine + 2.0f;
            }
            body += kBodyPad;
        } else {
            // 入力は左の辺、出力は右の辺に、一行に一つずつ。
            width = (std::max)(width, WidestLabel(node.inputs, node.inputCount) +
                                          WidestLabel(node.outputs, node.outputCount) + kLabelGap);
            layout.detailTop = body + kBodyPad * 0.5f;
            body += detailHeight;
            layout.inputLabelTop = body + kBodyPad * 0.5f;
            layout.outputLabelTop = layout.inputLabelTop;
            body += static_cast<float>((std::max)(node.inputCount, node.outputCount)) * rowHeight + kBodyPad;
        }
        layout.size = {(std::max)(kMinNodeWidth, width + kPadX * 2.0f), body};

        const auto addPins = [&](const EditorNodeGraphPin* pins, uint32_t count, bool output) {
            for (uint32_t pin = 0u; pin < count; ++pin) {
                PinLayout entry{&pins[pin], {}, index, output};
                if (tree) {
                    entry.position = {layout.position.x +
                                          layout.size.x * static_cast<float>(pin + 1u) / static_cast<float>(count + 1u),
                                      layout.position.y + (output ? layout.size.y : 0.0f)};
                } else {
                    entry.position = {layout.position.x + (output ? layout.size.x : 0.0f),
                                      layout.position.y + layout.inputLabelTop +
                                          (static_cast<float>(pin) + 0.5f) * rowHeight};
                }
                if (pins[pin].id != 0u) {
                    pinById_.emplace(pins[pin].id, static_cast<uint32_t>(pins_.size()));
                }
                pins_.push_back(entry);
            }
        };
        addPins(node.inputs, node.inputCount, false);
        addPins(node.outputs, node.outputCount, true);
        sizes_.push_back({layout.size.x, layout.size.y});
        nodes_.push_back(layout);
    }
}

void NodeGraphView::FrameNodes(const MATH::Vec2& viewportSize, bool selectedOnly) {
    MATH::Vec2 minimum{FLT_MAX, FLT_MAX};
    MATH::Vec2 maximum{-FLT_MAX, -FLT_MAX};
    for (const NodeLayout& node : nodes_) {
        if (selectedOnly && !node.selected) {
            continue;
        }
        minimum = {(std::min)(minimum.x, node.position.x), (std::min)(minimum.y, node.position.y)};
        maximum = {(std::max)(maximum.x, node.position.x + node.size.x),
                   (std::max)(maximum.y, node.position.y + node.size.y)};
    }
    if (minimum.x <= maximum.x) {
        // 少ない節を画面いっぱいに拡大しない（等倍まで）。
        NodeGraphCanvas::FrameBounds(canvas_, viewportSize, minimum, maximum, 80.0f, 0.35f, 1.0f);
    }
}

const NodeGraphView::PinLayout* NodeGraphView::FindPin(uint64_t pinId) const {
    const auto found = pinById_.find(pinId);
    return found != pinById_.end() ? &pins_[found->second] : nullptr;
}

int NodeGraphView::HitPin(const MATH::Vec2& pointer, float radius) const {
    for (size_t index = pins_.size(); index-- > 0u;) {
        if (pins_[index].pin->id != 0u && NodeGraphCanvas::HitPoint(pointer, pins_[index].position, radius)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int NodeGraphView::HitNode(const MATH::Vec2& pointer) const {
    // 選んだ節は上に描くので、先に当てる。
    for (const bool selected : {true, false}) {
        for (size_t index = nodes_.size(); index-- > 0u;) {
            const NodeLayout& node = nodes_[index];
            if (node.selected == selected &&
                NodeGraphCanvas::HitRect(pointer, node.position,
                                         {node.position.x + node.size.x, node.position.y + node.size.y})) {
                return static_cast<int>(index);
            }
        }
    }
    return -1;
}

std::array<MATH::Vec2, 4> NodeGraphView::LinkCurve(EditorNodeGraphFlow flow, const MATH::Vec2& start,
                                                   const MATH::Vec2& end) const {
    if (flow == EditorNodeGraphFlow::TopToBottom) {
        const float reach = (std::max)(30.0f, std::fabs(end.y - start.y) * 0.5f);
        return {start, MATH::Vec2{start.x, start.y + reach}, MATH::Vec2{end.x, end.y - reach}, end};
    }
    const float reach = (std::max)(40.0f, std::fabs(end.x - start.x) * 0.5f);
    return {start, MATH::Vec2{start.x + reach, start.y}, MATH::Vec2{end.x - reach, end.y}, end};
}

int NodeGraphView::HitLink(const EditorNodeGraphDesc& desc, const MATH::Vec2& pointer, float tolerance) const {
    for (uint32_t index = desc.linkCount; index-- > 0u;) {
        const PinLayout* output = FindPin(desc.links[index].outputPinId);
        const PinLayout* input = FindPin(desc.links[index].inputPinId);
        if (output == nullptr || input == nullptr) {
            continue;
        }
        const std::array<MATH::Vec2, 4> curve = LinkCurve(desc.flow, output->position, input->position);
        if (NodeGraphCanvas::HitBezierCubic(pointer, curve[0], curve[1], curve[2], curve[3], tolerance)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

} // namespace HIKARI::EDITOR
