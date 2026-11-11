#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"

#include <cmath>
#include <unordered_map>

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

// 点から線分までの距離の二乗。
float DistanceSquaredToSegment(const MATH::Vec2& point, const MATH::Vec2& start, const MATH::Vec2& end) noexcept {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = dx * dx + dy * dy;
    const float t =
        length > 0.0f ? std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / length, 0.0f, 1.0f) : 0.0f;
    const float x = start.x + dx * t - point.x;
    const float y = start.y + dy * t - point.y;
    return x * x + y * y;
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
        const bool moving = action_ == Action::MoveNodes &&
                            (desc.dragDescendants != 0u ? Contains(dragSet_, node.id) : layout.selected);
        layout.dragged = moving && desc.dragDescendants != 0u;
        if (moving) {
            layout.position.x += moveOffset_.x;
            layout.position.y += moveOffset_.y;
        }
        layout.firstPin = static_cast<uint32_t>(pins_.size());
        layout.headerHeight = lineHeight + kHeaderPadY * 2.0f;

        float width = MeasureText(View(node.title)).x;
        if (HasFlag(node.flags, EditorNodeGraphNodeFlag::Collapsible)) {
            width += lineHeight + kCollapseGap;
        }
        if (node.icon != EditorNodeGraphIcon::None) {
            width += lineHeight + kCollapseGap;
        }
        if (node.problem != EditorNodeGraphProblem::None) {
            width += lineHeight;
        }
        // 題の下の小字、行、進み具合、注（どの向きでも detail の続きに並ぶ）。
        float extraHeight = 0.0f;
        const auto addLine = [&](float lineWidth) {
            width = (std::max)(width, lineWidth);
            extraHeight += smallLine;
        };
        if (!View(node.subtitle).empty()) {
            addLine(MeasureText(View(node.subtitle), kSmallText).x);
        }
        for (uint32_t row = 0u; row < node.rowCount; ++row) {
            const EditorNodeGraphRow& item = node.rows[row];
            addLine(MeasureText(View(item.label), kSmallText).x + kLabelGap +
                    MeasureText(View(item.value), kSmallText).x + (item.capsule != 0u ? kCapsulePadX * 2.0f : 0.0f));
            extraHeight += item.capsule != 0u ? 2.0f : 0.0f;
        }
        if (node.progress >= 0.0f) {
            addLine(MeasureText(View(node.progressText), kSmallText).x);
            extraHeight += kProgressBarHeight + 3.0f;
        }
        uint32_t noteLines = 0u;
        ForEachLine(View(node.note), [&](std::string_view line) {
            if (noteLines++ < kNoteLines) {
                addLine(MeasureText(line, kSmallText).x);
            }
        });
        if (noteLines != 0u) {
            extraHeight += kBodyPad;
        }
        if (const std::string_view badge = View(node.badge); !badge.empty()) {
            width += kPadX + MeasureText(badge, kSmallText).x;
        }
        float detailHeight = extraHeight;
        ForEachLine(View(node.detail), [&](std::string_view line) {
            width = (std::max)(width, MeasureText(line, kSmallText).x);
            detailHeight += smallLine;
        });
        if (detailHeight > 0.0f) {
            detailHeight += kBodyPad;
        }
        // 幅を刻みに揃え、上限で切る（はみ出す字は描く時に「…」）。
        float nodeWidth = (std::max)(kMinNodeWidth, width + kPadX * 2.0f);
        if (desc.nodeWidthStep > 0.0f) {
            nodeWidth = std::ceil(nodeWidth / desc.nodeWidthStep) * desc.nodeWidthStep;
        }
        if (desc.nodeMaxWidth > 0.0f) {
            nodeWidth = (std::min)(nodeWidth, (std::max)(kMinNodeWidth, desc.nodeMaxWidth));
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
        layout.size = {nodeWidth, body};

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
        NodeGraphCanvas::FrameBounds(canvas_, viewportSize, minimum, maximum, 80.0f, kMinZoom, 1.0f);
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
            if (node.selected == selected && !node.dragged &&
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

std::array<MATH::Vec2, 4> NodeGraphView::LinkElbow(const MATH::Vec2& start, const MATH::Vec2& end) const {
    // 兄弟への線が同じ高さで横に並ぶよう、折れる高さは出力の少し下に揃える。
    const float bend = start.y + (std::max)(kElbowDrop, (std::min)((end.y - start.y) * 0.5f, kElbowDrop * 2.0f));
    return {start, MATH::Vec2{start.x, bend}, MATH::Vec2{end.x, bend}, end};
}

bool NodeGraphView::HitCollapse(const EditorNodeGraphNode& node, const NodeLayout& layout,
                                const MATH::Vec2& pointer) const {
    if (!HasFlag(node.flags, EditorNodeGraphNodeFlag::Collapsible)) {
        return false;
    }
    const float side = ImGui::GetFontSize();
    const MATH::Vec2 minimum{layout.position.x + kPadX * 0.5f, layout.position.y};
    return NodeGraphCanvas::HitRect(pointer, minimum,
                                    {minimum.x + side + kCollapseGap, minimum.y + layout.headerHeight});
}

void NodeGraphView::CollectDragSet(const EditorNodeGraphDesc& desc, uint64_t nodeId) {
    dragSet_.assign(1u, nodeId);
    if (desc.dragDescendants == 0u) {
        return;
    }
    // 出力の引脚の節 → 入力の引脚の節を辿る。循環していても一度ずつ。
    std::unordered_multimap<uint64_t, uint64_t> children{};
    for (uint32_t index = 0u; index < desc.linkCount; ++index) {
        const PinLayout* output = FindPin(desc.links[index].outputPinId);
        const PinLayout* input = FindPin(desc.links[index].inputPinId);
        if (output != nullptr && input != nullptr) {
            children.emplace(desc.nodes[output->node].id, desc.nodes[input->node].id);
        }
    }
    for (size_t next = 0u; next < dragSet_.size(); ++next) {
        const auto [begin, end] = children.equal_range(dragSet_[next]);
        for (auto child = begin; child != end; ++child) {
            if (!Contains(dragSet_, child->second)) {
                dragSet_.push_back(child->second);
            }
        }
    }
}

void NodeGraphView::FollowNode(const EditorNodeGraphDesc& desc, const Frame& frame) {
    if (desc.followNodeId == 0u || action_ != Action::None) {
        return;
    }
    for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
        if (desc.nodes[index].id != desc.followNodeId) {
            continue;
        }
        const NodeLayout& node = nodes_[index];
        const MATH::Vec2 minimum = NodeGraphCanvas::CanvasToScreen(canvas_, frame.origin, node.position);
        const MATH::Vec2 maximum = NodeGraphCanvas::CanvasToScreen(
            canvas_, frame.origin, {node.position.x + node.size.x, node.position.y + node.size.y});
        const float margin = 24.0f;
        if (minimum.x >= frame.origin.x + margin && minimum.y >= frame.origin.y + margin &&
            maximum.x <= frame.origin.x + frame.size.x - margin &&
            maximum.y <= frame.origin.y + frame.size.y - margin) {
            return;
        }
        // 節の真ん中を画面の真ん中へ。
        canvas_.pan = {frame.size.x * 0.5f - (node.position.x + node.size.x * 0.5f) * canvas_.zoom,
                       frame.size.y * 0.5f - (node.position.y + node.size.y * 0.5f) * canvas_.zoom};
        return;
    }
}

int NodeGraphView::HitLink(const EditorNodeGraphDesc& desc, const MATH::Vec2& pointer, float tolerance) const {
    for (uint32_t index = desc.linkCount; index-- > 0u;) {
        const PinLayout* output = FindPin(desc.links[index].outputPinId);
        const PinLayout* input = FindPin(desc.links[index].inputPinId);
        if (output == nullptr || input == nullptr) {
            continue;
        }
        if (desc.linkStyle == EditorNodeGraphLinkStyle::Elbow) {
            // 兄弟への線は折れる所から上が重なるので、自分だけの横と下りの部分で当てる。
            const std::array<MATH::Vec2, 4> elbow = LinkElbow(output->position, input->position);
            const bool across = DistanceSquaredToSegment(pointer, elbow[1], elbow[2]) <= tolerance * tolerance &&
                                std::fabs(pointer.x - elbow[1].x) > tolerance;
            if (across || DistanceSquaredToSegment(pointer, elbow[2], elbow[3]) <= tolerance * tolerance) {
                return static_cast<int>(index);
            }
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
