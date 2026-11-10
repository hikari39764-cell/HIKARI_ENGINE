#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"

#include <cmath>
#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/NodeGraph/HIKARI_NodeGraphViewInternal.h"

// 節点図の操作：一フレームの流れ、選択、鼠標とキー、右クリックの献立。
namespace HIKARI::EDITOR {

using namespace EDITOR_PROJECT;
using namespace NODE_GRAPH_VIEW;

namespace {

constexpr const char* kMenuId = "NodeGraphMenu";

bool KindsMatch(uint32_t lhs, uint32_t rhs) noexcept { return lhs == 0u || rhs == 0u || lhs == rhs; }

} // namespace

void NodeGraphView::ApplyRequestedSelection(const EditorNodeGraphDesc& desc) {
    if (desc.replaceSelection != 0u) {
        selectedNodes_.assign(desc.selectNodeIds, desc.selectNodeIds + desc.selectNodeCount);
        selectedLinks_.clear();
    }
    // 呼び手が消した節と線は選択からも外す。
    std::erase_if(selectedNodes_, [&](uint64_t id) {
        return std::none_of(desc.nodes, desc.nodes + desc.nodeCount,
                            [id](const EditorNodeGraphNode& node) { return node.id == id; });
    });
    std::erase_if(selectedLinks_, [&](uint64_t id) {
        return std::none_of(desc.links, desc.links + desc.linkCount,
                            [id](const EditorNodeGraphLink& link) { return link.id == id; });
    });
}

void NodeGraphView::SelectNode(uint64_t nodeId, bool toggle, bool additive) {
    const auto found = std::find(selectedNodes_.begin(), selectedNodes_.end(), nodeId);
    if (toggle) {
        if (found != selectedNodes_.end()) {
            selectedNodes_.erase(found);
        } else {
            selectedNodes_.push_back(nodeId);
        }
        return;
    }
    if (found != selectedNodes_.end()) {
        return;
    }
    if (!additive) {
        selectedNodes_.clear();
        selectedLinks_.clear();
    }
    selectedNodes_.push_back(nodeId);
}

void NodeGraphView::OpenMenu(MenuTarget target, const MATH::Vec2& position, uint64_t fromPinId) {
    menuTarget_ = target;
    menuPosition_ = position;
    menuFromPinId_ = fromPinId;
    menuFilter_.fill('\0');
    ImGui::OpenPopup(kMenuId);
}

void NodeGraphView::BeginAction(const EditorNodeGraphDesc& desc, const Frame& frame, EditorNodeGraphResult& outResult) {
    if (action_ != Action::None || !frame.hovered) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (frame.pin >= 0 && frame.editable) {
            action_ = Action::DragLink;
            dragPinId_ = pins_[frame.pin].pin->id;
        } else if (frame.node >= 0) {
            const uint64_t nodeId = desc.nodes[frame.node].id;
            SelectNode(nodeId, io.KeyCtrl, io.KeyShift);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                outResult.activatedNodeId = nodeId;
            } else if (frame.editable && Contains(selectedNodes_, nodeId)) {
                action_ = Action::MoveNodes;
                moveOffset_ = {};
            }
        } else if (frame.link >= 0) {
            const uint64_t linkId = desc.links[frame.link].id;
            if (!io.KeyCtrl && !io.KeyShift) {
                selectedNodes_.clear();
                selectedLinks_.clear();
            }
            if (const auto found = std::find(selectedLinks_.begin(), selectedLinks_.end(), linkId);
                found != selectedLinks_.end()) {
                selectedLinks_.erase(found);
            } else {
                selectedLinks_.push_back(linkId);
            }
        } else {
            action_ = Action::BoxSelect;
            boxStart_ = frame.pointer;
            boxAdditive_ = io.KeyCtrl || io.KeyShift;
            if (!boxAdditive_) {
                selectedNodes_.clear();
                selectedLinks_.clear();
            }
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        action_ = Action::Pan;
        panButton_ = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
        panDistance_ = 0.0f;
    }
}

bool NodeGraphView::ContinueAction(const EditorNodeGraphDesc& desc, const Frame& frame,
                                   EditorNodeGraphResult& outResult) {
    const ImGuiIO& io = ImGui::GetIO();
    switch (action_) {
    case Action::Pan:
        if (ImGui::IsMouseDown(panButton_)) {
            NodeGraphCanvas::Pan(canvas_, {io.MouseDelta.x, io.MouseDelta.y});
            panDistance_ += std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y);
            break;
        }
        action_ = Action::None;
        if (panButton_ == ImGuiMouseButton_Right && panDistance_ < kClickSlop) {
            // 動かさずに離した右クリックは献立。節や線の上なら、それを選んでから開く。
            if (frame.node >= 0) {
                SelectNode(desc.nodes[frame.node].id, false, false);
                OpenMenu(MenuTarget::Node, frame.pointer, 0u);
            } else if (frame.link >= 0) {
                selectedNodes_.clear();
                selectedLinks_.assign(1u, desc.links[frame.link].id);
                OpenMenu(MenuTarget::Link, frame.pointer, 0u);
            } else {
                OpenMenu(MenuTarget::Canvas, frame.pointer, 0u);
            }
        }
        break;
    case Action::MoveNodes:
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            moveOffset_.x += io.MouseDelta.x / canvas_.zoom;
            moveOffset_.y += io.MouseDelta.y / canvas_.zoom;
            break;
        }
        // 今のフレームはずらした位置のまま描き、描いた後で呼び手へ渡す（一フレーム戻って見えるのを避ける）。
        if (std::fabs(moveOffset_.x) + std::fabs(moveOffset_.y) >= 0.5f) {
            return true;
        }
        action_ = Action::None;
        break;
    case Action::DragLink:
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            action_ = Action::None;
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            action_ = Action::None;
            const PinLayout* from = FindPin(dragPinId_);
            const int target = HitPin(frame.pointer, kPinHitRadius / canvas_.zoom);
            if (from != nullptr && target >= 0) {
                const PinLayout& to = pins_[target];
                if (to.output != from->output && to.node != from->node && KindsMatch(to.pin->kind, from->pin->kind)) {
                    outResult.linkCreated = 1u;
                    outResult.linkOutputPinId = from->output ? from->pin->id : to.pin->id;
                    outResult.linkInputPinId = from->output ? to.pin->id : from->pin->id;
                }
            } else if (from != nullptr && frame.hovered && frame.node < 0 && desc.createItemCount != 0u) {
                // 何も無い所で離したら、そこに作る節を選ばせる。作った節と繋ぐのは呼び手。
                OpenMenu(MenuTarget::Canvas, frame.pointer, dragPinId_);
            }
        }
        break;
    case Action::BoxSelect:
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            action_ = Action::None;
            const MATH::Vec2 minimum{(std::min)(boxStart_.x, frame.pointer.x),
                                     (std::min)(boxStart_.y, frame.pointer.y)};
            const MATH::Vec2 maximum{(std::max)(boxStart_.x, frame.pointer.x),
                                     (std::max)(boxStart_.y, frame.pointer.y)};
            if ((maximum.x - minimum.x + maximum.y - minimum.y) * canvas_.zoom < kClickSlop) {
                break;
            }
            for (size_t index = 0u; index < nodes_.size(); ++index) {
                const NodeLayout& node = nodes_[index];
                const bool inside = node.position.x <= maximum.x && node.position.x + node.size.x >= minimum.x &&
                                    node.position.y <= maximum.y && node.position.y + node.size.y >= minimum.y;
                if (inside && !Contains(selectedNodes_, desc.nodes[index].id)) {
                    selectedNodes_.push_back(desc.nodes[index].id);
                }
            }
        }
        break;
    case Action::None: break;
    }
    return false;
}

void NodeGraphView::HandleKeys(const EditorNodeGraphDesc& desc, const Frame& frame, EditorNodeGraphResult& outResult) {
    const ImGuiIO& io = ImGui::GetIO();
    if (action_ != Action::None || (!frame.hovered && !frame.focused) || io.WantTextInput ||
        ImGui::IsPopupOpen(kMenuId)) {
        return;
    }
    const bool hasNodes = !selectedNodes_.empty();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && frame.editable && (hasNodes || !selectedLinks_.empty())) {
        outResult.deleteRequested = 1u;
    }
    if (!io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            // 選んだ節があればそこへ、無ければ全体へ視点を合わせる。
            FrameNodes(frame.size, hasNodes);
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C, false) && hasNodes) {
        outResult.copyRequested = 1u;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_X, false) && hasNodes && frame.editable) {
        outResult.cutRequested = 1u;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D, false) && hasNodes && frame.editable) {
        outResult.duplicateRequested = 1u;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_V, false) && frame.editable) {
        // ポインタが図の上ならそこへ、外なら図の真ん中へ貼る。
        const MATH::Vec2 center = NodeGraphCanvas::ScreenToCanvas(
            canvas_, frame.origin, {frame.origin.x + frame.size.x * 0.5f, frame.origin.y + frame.size.y * 0.5f});
        const MATH::Vec2 position = frame.hovered ? frame.pointer : center;
        outResult.pasteRequested = 1u;
        outResult.pastePosition = {position.x, position.y};
    }
    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        selectedLinks_.clear();
        selectedNodes_.clear();
        for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
            selectedNodes_.push_back(desc.nodes[index].id);
        }
    }
}

void NodeGraphView::DrawContextMenu(const EditorNodeGraphDesc& desc, EditorNodeGraphResult& outResult) {
    if (!ImGui::BeginPopup(kMenuId)) {
        return;
    }
    const bool editable = desc.readOnly == 0u;
    const auto item = [](const char* key, const char* shortcut) {
        return ImGui::MenuItem(TEXT::Localize(key).c_str(), shortcut) ? 1u : 0u;
    };
    if (menuTarget_ == MenuTarget::Node) {
        outResult.copyRequested |= item("ui.nodeGraph.copy", "Ctrl+C");
        if (editable) {
            outResult.cutRequested |= item("ui.nodeGraph.cut", "Ctrl+X");
            outResult.duplicateRequested |= item("ui.nodeGraph.duplicate", "Ctrl+D");
            ImGui::Separator();
            outResult.deleteRequested |= item("ui.common.delete", "Del");
        }
        if (desc.nodeActionCount != 0u) {
            ImGui::Separator();
        }
        for (uint32_t index = 0u; index < desc.nodeActionCount; ++index) {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::MenuItem(desc.nodeActions[index].label != nullptr ? desc.nodeActions[index].label : "")) {
                outResult.nodeActionIndex = static_cast<int32_t>(index);
            }
            ImGui::PopID();
        }
    } else if (menuTarget_ == MenuTarget::Link) {
        if (editable) {
            outResult.deleteRequested |= item("ui.common.delete", "Del");
        }
    } else {
        if (editable && desc.createItemCount != 0u) {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.nodeGraph.addNode").c_str());
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##NodeGraphFilter", menuFilter_.data(), menuFilter_.size());
            const std::string filter = TEXT::ToLowerAsciiCopy(menuFilter_.data());
            const auto choose = [&](uint32_t index) {
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::MenuItem(desc.createItems[index].label != nullptr ? desc.createItems[index].label : "")) {
                    outResult.createItemIndex = static_cast<int32_t>(index);
                    outResult.createPosition = {menuPosition_.x, menuPosition_.y};
                    outResult.createFromPinId = menuFromPinId_;
                }
                ImGui::PopID();
            };
            for (uint32_t index = 0u; index < desc.createItemCount; ++index) {
                const std::string_view category = View(desc.createItems[index].category);
                if (!filter.empty()) {
                    // 絞り込み中は小見出しを開かずに、合う種類を並べる。
                    if (TEXT::ToLowerAsciiCopy(View(desc.createItems[index].label)).find(filter) != std::string::npos) {
                        choose(index);
                    }
                } else if (category.empty()) {
                    choose(index);
                } else if (std::none_of(desc.createItems, desc.createItems + index,
                                        [&](const EditorNodeGraphMenuItem& earlier) {
                                            return View(earlier.category) == category;
                                        }) &&
                           ImGui::BeginMenu(desc.createItems[index].category)) {
                    // 小見出しは、その名前が最初に出た所に一度だけ作り、同じ名前の種類を全部入れる。
                    for (uint32_t member = index; member < desc.createItemCount; ++member) {
                        if (View(desc.createItems[member].category) == category) {
                            choose(member);
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
        }
        // 引脚から線を引いて開いた献立は、節を作るためだけのもの。
        if (menuFromPinId_ == 0u) {
            if (editable && item("ui.nodeGraph.paste", "Ctrl+V") != 0u) {
                outResult.pasteRequested = 1u;
                outResult.pastePosition = {menuPosition_.x, menuPosition_.y};
            }
            if (item("ui.nodeGraph.frameAll", "F") != 0u) {
                framed_ = false;
            }
        }
    }
    ImGui::EndPopup();
}

void NodeGraphView::Draw(const char* stableId, const EditorNodeGraphDesc& desc, EditorNodeGraphResult& outResult) {
    outResult = {};
    moved_.clear();
    const std::vector<uint64_t> previousNodes = selectedNodes_;
    const std::vector<uint64_t> previousLinks = selectedLinks_;
    ApplyRequestedSelection(desc);
    const auto publishSelection = [&]() {
        outResult.selectedNodeIds = selectedNodes_.data();
        outResult.selectedNodeCount = static_cast<uint32_t>(selectedNodes_.size());
        outResult.selectedLinkIds = selectedLinks_.data();
        outResult.selectedLinkCount = static_cast<uint32_t>(selectedLinks_.size());
        // 呼び手が自分で置き換えた選択は「変わった」と知らせない。
        outResult.selectionChanged =
            desc.replaceSelection == 0u && (previousNodes != selectedNodes_ || previousLinks != selectedLinks_) ? 1u
                                                                                                                : 0u;
    };

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size{(std::max)(64.0f, available.x),
                      desc.height > 0.0f ? desc.height : (std::max)(64.0f, available.y)};
    // 子窓にして、ホイールで親の窓が動かないようにする。
    if (!ImGui::BeginChild(stableId, size, ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                               ImGuiWindowFlags_NoMove)) {
        ImGui::EndChild();
        publishSelection();
        return;
    }

    Frame frame{};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    frame.origin = {origin.x, origin.y};
    frame.size = {size.x, size.y};
    ImGui::InvisibleButton("NodeGraphCanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    frame.hovered = ImGui::IsItemHovered();
    frame.focused = ImGui::IsItemFocused();
    frame.editable = desc.readOnly == 0u;
    const ImGuiIO& io = ImGui::GetIO();
    if (frame.hovered && io.MouseWheel != 0.0f) {
        NodeGraphCanvas::ZoomAt(canvas_, {io.MousePos.x, io.MousePos.y}, frame.origin, io.MouseWheel);
    }

    BuildLayout(desc);
    if (!framed_ || desc.frameAll != 0u) {
        FrameNodes(frame.size, false);
        // 窓がドックに収まるまでの数フレームは大きさが変わるので、落ち着くまで合わせ直す。
        framed_ = frame.size.x == framedSize_.x && frame.size.y == framedSize_.y;
        framedSize_ = frame.size;
    }

    frame.pointer = NodeGraphCanvas::ScreenToCanvas(canvas_, frame.origin, {io.MousePos.x, io.MousePos.y});
    if (frame.hovered) {
        frame.pin = HitPin(frame.pointer, kPinHitRadius / canvas_.zoom);
        frame.node = frame.pin < 0 ? HitNode(frame.pointer) : -1;
        frame.link = frame.pin < 0 && frame.node < 0 ? HitLink(desc, frame.pointer, 6.0f / canvas_.zoom) : -1;
    }
    if (frame.node >= 0) {
        outResult.hoveredNodeId = desc.nodes[frame.node].id;
    }

    BeginAction(desc, frame, outResult);
    const bool commitMove = ContinueAction(desc, frame, outResult);
    HandleKeys(desc, frame, outResult);
    // 今のフレームで変わった選択を、そのまま描画に出す。
    for (size_t index = 0u; index < nodes_.size(); ++index) {
        nodes_[index].selected = Contains(selectedNodes_, desc.nodes[index].id);
    }
    Paint(desc, frame);
    DrawContextMenu(desc, outResult);
    ImGui::EndChild();

    if (commitMove) {
        for (uint32_t index = 0u; index < desc.nodeCount; ++index) {
            if (nodes_[index].selected) {
                moved_.push_back({desc.nodes[index].id,
                                  {std::round(nodes_[index].position.x), std::round(nodes_[index].position.y)}});
            }
        }
        action_ = Action::None;
        moveOffset_ = {};
    }
    outResult.movedNodes = moved_.data();
    outResult.movedNodeCount = static_cast<uint32_t>(moved_.size());
    outResult.nodeSizes = sizes_.data();
    outResult.nodeSizeCount = static_cast<uint32_t>(sizes_.size());
    publishSelection();
}

} // namespace HIKARI::EDITOR
