#include "Editor/Widgets/HIKARI_ScalarCurveEditor.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
using Gesture = ScalarCurveView::Gesture;
constexpr ImU32 kSelected = IM_COL32(255, 202, 96, 255);
constexpr ImU32 kColors[]{IM_COL32(92, 211, 222, 255), IM_COL32(238, 120, 123, 255), IM_COL32(131, 211, 146, 255),
                          IM_COL32(112, 161, 244, 255), IM_COL32(213, 163, 234, 255)};
float Distance(ImVec2 a, ImVec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
void Fit(std::span<const CurveEditChannel> channels, ScalarCurveView& view) {
    float loT = std::numeric_limits<float>::max(), hiT = -loT, loV = loT, hiV = hiT;
    for (const auto& c : channels) {
        if (!c.visible)
            continue;
        for (size_t i = 0; i < c.curve->keys.size(); ++i) {
            if (view.fitSelection && !view.editing.Contains(c.id, i))
                continue;
            const auto& k = c.curve->keys[i];
            loT = (std::min)(loT, k.time + c.offset);
            hiT = (std::max)(hiT, k.time + c.offset);
        }
        if (!view.fitSelection) {
            loT = (std::min)(loT, c.offset);
            hiT = (std::max)(hiT, c.offset + c.end);
        }
    }
    if (loT > hiT) {
        loT = 0;
        hiT = 1;
    }
    const float timeMargin = (std::max)(.05f, (hiT - loT) * .05f);
    view.timeStart = loT - timeMargin;
    view.duration = (std::max)(.1f, hiT - loT + 2 * timeMargin);
    for (const auto& c : channels)
        if (c.visible)
            for (int i = 0; i <= 256; ++i) {
                const float time = loT + (hiT - loT) * i / 256 - c.offset;
                const float v = (view.speedGraph ? c.curve->Velocity(time) : c.curve->Evaluate(time)) * c.valueScale +
                                (view.speedGraph ? 0 : c.valueOffset);
                loV = (std::min)(loV, v);
                hiV = (std::max)(hiV, v);
            }
    if (loV > hiV) {
        loV = -1;
        hiV = 1;
    }
    const float margin = (std::max)(.25f, (hiV - loV) * .2f);
    view.low = loV - margin;
    view.high = hiV + margin;
    view.fitted = true;
}
} // namespace
bool DrawCurveEditorCanvas(std::span<CurveEditChannel> channels, float playhead, ScalarCurveView& view,
                           const CurveCanvasLayout& layout) {
    auto& edit = view.editing;
    edit.Prune(channels);
    if (!layout.lanes && !view.fitted)
        Fit(channels, view);
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{(std::max)(80.f, layout.size.x), (std::max)(28.f, layout.size.y)};
    const float left = origin.x + (layout.lanes ? layout.labelWidth : 48.f);
    const float clipLeft = layout.lanes ? (std::max)(left, ImGui::GetWindowPos().x + layout.labelWidth) : left;
    const float top = origin.y + (layout.lanes ? 0 : 24.f);
    const float bottom = origin.y + size.y - (layout.lanes ? 0 : 20.f);
    const float width = (std::max)(1.f, origin.x + size.x - left - 8);
    const float zoom = layout.lanes ? layout.pixelsPerSecond : width / view.duration;
    const float start = layout.lanes ? layout.timeStart : view.timeStart;
    const auto xAt = [&](float t) { return left + (t - start) * zoom; };
    const auto timeAt = [&](float x) { return start + (x - left) / zoom; };
    const auto yAt = [&](float v) { return top + (view.high - v) / (view.high - view.low) * (bottom - top); };
    const auto valueAt = [&](float y) { return view.high - (y - top) / (bottom - top) * (view.high - view.low); };
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(14, 20, 27, 255), 4);
    // キャンバスは独自のキー操作を持つ。自動 Nav フォーカスが初回のマウスドラッグを解除しないようにする。
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::InvisibleButton("curveCanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_NoNavFocus);
    ImGui::PopItemFlag();
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const auto& io = ImGui::GetIO();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool changed = false;
    draw->PushClipRect({clipLeft, origin.y}, {origin.x + size.x, origin.y + size.y}, true);
    if (!layout.lanes) {
        for (int i = 0; i <= 4; ++i) {
            const float t = start + view.duration * i / 4, v = view.low + (view.high - view.low) * i / 4;
            char label[32];
            std::snprintf(label, sizeof(label), "%.2fs", t);
            draw->AddText({xAt(t), origin.y + 2}, IM_COL32(150, 165, 183, 255), label);
            draw->AddLine({xAt(t), top}, {xAt(t), bottom}, IM_COL32(34, 44, 55, 255));
            draw->AddLine({left, yAt(v)}, {origin.x + size.x, yAt(v)}, IM_COL32(34, 44, 55, 255));
        }
        if (view.low <= 0 && view.high >= 0)
            draw->AddLine({left, yAt(0)}, {origin.x + size.x, yAt(0)}, IM_COL32(95, 110, 125, 255));
    }
    struct Hit {
        CurveKeyRef key;
        Gesture gesture = Gesture::Keys;
        ImVec2 position{};
        bool weight = true;
    };
    std::vector<Hit> dots;
    std::optional<Hit> hit;
    float hitDistance = 81;
    const auto consider = [&](const Hit& item) {
        const float d = Distance(io.MousePos, item.position);
        if (hovered && io.MousePos.x >= clipLeft && d < hitDistance) {
            hit = item;
            hitDistance = d;
        }
    };
    size_t row = 0;
    for (auto& c : channels) {
        if (!c.visible)
            continue;
        const float middle = origin.y + (static_cast<float>(row) + .5f) * layout.rowHeight;
        const ImU32 color = kColors[row % std::size(kColors)];
        ++row;
        if (layout.lanes)
            draw->AddLine({left, middle + layout.rowHeight * .5f}, {origin.x + size.x, middle + layout.rowHeight * .5f},
                          IM_COL32(36, 46, 56, 255));
        else {
            ImVec2 previous{};
            for (int i = 0; i <= 256; ++i) {
                const float time = start + view.duration * i / 256 - c.offset;
                const float value =
                    (view.speedGraph ? c.curve->Velocity(time) : c.curve->Evaluate(time)) * c.valueScale +
                    (view.speedGraph ? 0 : c.valueOffset);
                ImVec2 p{xAt(time + c.offset), yAt(value)};
                if (i)
                    draw->AddLine(previous, p, color, 1.7f);
                previous = p;
            }
        }
        for (size_t i = 0; i < c.curve->keys.size(); ++i) {
            const auto key = ResolvedScalarKey(c.curve->keys, i);
            const float v = (view.speedGraph && !layout.lanes ? key.outSlope : key.value) * c.valueScale +
                            (view.speedGraph && !layout.lanes ? 0 : c.valueOffset);
            const ImVec2 p{xAt(key.time + c.offset), layout.lanes ? middle : yAt(v)};
            const CurveKeyRef ref{c.id, i};
            const bool selected = edit.Contains(c.id, i);
            Hit dot{ref, view.speedGraph && !layout.lanes ? Gesture::OutHandle : Gesture::Keys, p, false};
            if (view.speedGraph && !layout.lanes && i + 1 == c.curve->keys.size()) {
                dot.gesture = Gesture::InHandle;
                dot.position.y = yAt(key.inSlope * c.valueScale);
            }
            dots.push_back(dot);
            consider(dot);
            const auto q = dot.position;
            if (key.interpolation == ScalarCurveInterpolation::Hold)
                draw->AddRectFilled({q.x - 4, q.y - 4}, {q.x + 4, q.y + 4}, selected ? kSelected : color);
            else {
                const ImVec2 points[]{{q.x, q.y - 5}, {q.x + 5, q.y}, {q.x, q.y + 5}, {q.x - 5, q.y}};
                draw->AddConvexPolyFilled(points, 4, selected ? kSelected : color);
            }
            if (CurveKeyEditing::Locked(c, i))
                draw->AddCircle(q, 8, IM_COL32(185, 190, 198, 200));
            if (!selected || layout.lanes)
                continue;
            for (const int side : {-1, 1}) {
                if ((side < 0 && !i) || (side > 0 && i + 1 == c.curve->keys.size()))
                    continue;
                const float dt = side < 0 ? key.time - c.curve->keys[i - 1].time : c.curve->keys[i + 1].time - key.time;
                const float slope = side < 0 ? key.inSlope : key.outSlope;
                const float weight = side < 0 ? key.shape.inWeight : key.shape.outWeight;
                const ImVec2 base{xAt(key.time + c.offset), view.speedGraph ? yAt(slope * c.valueScale) : p.y};
                const ImVec2 handle{xAt(key.time + c.offset + side * dt * weight),
                                    view.speedGraph
                                        ? base.y
                                        : yAt((key.value + side * dt * weight * slope) * c.valueScale + c.valueOffset)};
                draw->AddLine(base, handle, kSelected);
                draw->AddCircle(handle, 4, kSelected, 0, 1.5f);
                consider({ref, side < 0 ? Gesture::InHandle : Gesture::OutHandle, handle});
                if (view.speedGraph)
                    consider({ref, side < 0 ? Gesture::InHandle : Gesture::OutHandle, base, false});
            }
        }
    }
    // 選択全体の左右ハンドルで時間を伸縮する。縦方向の値は変えない。
    float minTime = std::numeric_limits<float>::max(), maxTime = -minTime;
    for (const auto& c : channels)
        for (size_t i = 0; i < c.curve->keys.size(); ++i)
            if (edit.Contains(c.id, i)) {
                minTime = (std::min)(minTime, c.offset + c.curve->keys[i].time);
                maxTime = (std::max)(maxTime, c.offset + c.curve->keys[i].time);
            }
    if (edit.selection.size() > 1 && maxTime > minTime && !layout.lanes) {
        const float y = bottom + 9;
        draw->AddLine({xAt(minTime), y}, {xAt(maxTime), y}, kSelected);
        for (int side : {-1, 1}) {
            const ImVec2 p{xAt(side < 0 ? minTime : maxTime), y};
            draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, kSelected);
            consider({{}, side < 0 ? Gesture::ScaleLeft : Gesture::ScaleRight, p});
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        view.cancelled = false;
        view.mouseStart = io.MousePos;
        view.boxBefore = edit.selection;
        if (!layout.lanes && io.MousePos.y < top)
            view.seekTime = timeAt(io.MousePos.x);
        else if (hit) {
            view.grabbed = hit->key;
            view.handleWeight = hit->weight;
            view.gesture = hit->gesture;
            if (view.gesture == Gesture::ScaleLeft || view.gesture == Gesture::ScaleRight) {
                view.rangeStart = minTime;
                view.rangeEnd = maxTime;
            } else {
                edit.Select(hit->key, io.KeyShift);
                view.activeChannel = hit->key.channel;
                for (const auto& c : channels)
                    if (c.id == hit->key.channel)
                        view.grabbedTime = c.offset + c.curve->keys[hit->key.index].time;
                if (!edit.Contains(hit->key.channel, hit->key.index))
                    view.gesture = Gesture::None;
            }
            edit.Begin(channels);
        } else if (io.MousePos.x >= clipLeft) {
            view.gesture = Gesture::Box;
            if (!io.KeyShift) {
                edit.selection.clear();
                view.boxBefore.clear();
            }
        } else {
            size_t selectedRow = static_cast<size_t>((io.MousePos.y - origin.y) / layout.rowHeight), n = 0;
            for (const auto& c : channels)
                if (c.visible && n++ == selectedRow)
                    view.activeChannel = c.id;
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !view.cancelled) {
        if (view.gesture == Gesture::Box) {
            const ImVec2 lo{(std::min)(view.mouseStart.x, io.MousePos.x), (std::min)(view.mouseStart.y, io.MousePos.y)};
            const ImVec2 hi{(std::max)(view.mouseStart.x, io.MousePos.x), (std::max)(view.mouseStart.y, io.MousePos.y)};
            edit.selection = view.boxBefore;
            for (const auto& dot : dots)
                if (dot.position.x >= lo.x && dot.position.x <= hi.x && dot.position.y >= lo.y &&
                    dot.position.y <= hi.y)
                    edit.Select(dot.key, true);
            draw->AddRectFilled(lo, hi, IM_COL32(100, 186, 230, 35));
            draw->AddRect(lo, hi, IM_COL32(100, 186, 230, 220));
        } else if (view.gesture == Gesture::Keys) {
            float dt = (io.MousePos.x - view.mouseStart.x) / zoom;
            if (view.snap && !io.KeyAlt) {
                const float next = view.grabbedTime + dt;
                dt = std::round(next * view.fps) / view.fps - view.grabbedTime;
                if (std::abs(xAt(next) - xAt(playhead)) < 7)
                    dt = playhead - view.grabbedTime;
                for (const auto& c : channels)
                    if (c.visible)
                        for (float edge : {c.offset, c.offset + c.end})
                            if (std::abs(xAt(next) - xAt(edge)) < 5)
                                dt = edge - view.grabbedTime;
            }
            const float dv = layout.lanes || view.axis == 1 ? 0 : valueAt(io.MousePos.y) - valueAt(view.mouseStart.y);
            changed |= edit.Transform(channels, view.axis == 2 ? 0 : dt, dv);
        } else if (view.gesture == Gesture::ScaleLeft || view.gesture == Gesture::ScaleRight) {
            const float pivot = view.gesture == Gesture::ScaleLeft ? view.rangeEnd : view.rangeStart;
            const float edge = view.gesture == Gesture::ScaleLeft ? view.rangeStart : view.rangeEnd;
            float next = timeAt(io.MousePos.x);
            if (view.snap && !io.KeyAlt)
                next = std::round(next * view.fps) / view.fps;
            changed |= edit.Transform(channels, 0, 0, (next - pivot) / (edge - pivot), pivot);
        } else if (view.gesture == Gesture::InHandle || view.gesture == Gesture::OutHandle) {
            for (auto& c : channels)
                if (c.id == view.grabbed.channel && view.grabbed.index < c.curve->keys.size()) {
                    const size_t i = view.grabbed.index;
                    const bool incoming = view.gesture == Gesture::InHandle;
                    if ((incoming && !i) || (!incoming && i + 1 == c.curve->keys.size()))
                        continue;
                    const auto k = ResolvedScalarKey(c.curve->keys, i);
                    const float side = incoming ? -1.f : 1.f;
                    const float dt = incoming ? k.time - c.curve->keys[i - 1].time : c.curve->keys[i + 1].time - k.time;
                    float weight = incoming ? k.shape.inWeight : k.shape.outWeight;
                    if (view.handleWeight)
                        weight = std::clamp(side * (timeAt(io.MousePos.x) - c.offset - k.time) / dt, .001f, .999f);
                    const float slope = view.speedGraph
                                            ? valueAt(io.MousePos.y) / c.valueScale
                                            : ((valueAt(io.MousePos.y) - c.valueOffset) / c.valueScale - k.value) /
                                                  (side * dt * weight);
                    changed |= CurveKeyEditing::SetHandle(c, i, incoming, slope, weight, io.KeyAlt);
                }
        } else if (!layout.lanes && view.mouseStart.y < top)
            view.seekTime = timeAt(io.MousePos.x);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        view.gesture = Gesture::None;
        edit.End();
        view.cancelled = false;
    }
    draw->AddLine({xAt(playhead), origin.y}, {xAt(playhead), origin.y + size.y}, IM_COL32(230, 239, 248, 180));
    draw->PopClipRect();
    if (!layout.lanes) {
        for (int i = 0; i <= 4; ++i) {
            const float v = view.low + (view.high - view.low) * i / 4;
            char label[32];
            std::snprintf(label, sizeof(label), "%.2f", v);
            draw->AddText({origin.x + 2, yAt(v)}, IM_COL32(150, 165, 183, 255), label);
        }
        if (hovered && io.MouseWheel != 0) {
            const float factor = std::pow(1.15f, -io.MouseWheel);
            if (io.KeyShift) {
                const float value = valueAt(io.MousePos.y);
                view.low = value + (view.low - value) * factor;
                view.high = value + (view.high - value) * factor;
            } else {
                const float t = timeAt(io.MousePos.x), fraction = (t - view.timeStart) / view.duration;
                view.duration = std::clamp(view.duration * factor, .01f, 100000.f);
                view.timeStart = t - fraction * view.duration;
            }
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            view.timeStart -= io.MouseDelta.x / zoom;
            const float dy = io.MouseDelta.y / (bottom - top) * (view.high - view.low);
            view.low += dy;
            view.high += dy;
        }
    }
    if (hovered && hit && !ImGui::IsMouseDragging(0))
        ImGui::SetTooltip("%s · #%zu%s", hit->key.channel.c_str(), hit->key.index + 1,
                          hit->gesture == Gesture::Keys ? "" : " · 拖动调整速度与影响范围；Alt 拆分切线");
    if (hovered && ImGui::IsMouseDoubleClicked(0) && !hit && io.MousePos.x >= left &&
        (layout.lanes || io.MousePos.y >= top)) {
        float t = timeAt(io.MousePos.x);
        if (view.snap && !io.KeyAlt)
            t = std::round(t * view.fps) / view.fps;
        auto target =
            std::find_if(channels.begin(), channels.end(), [&](const auto& c) { return c.id == view.activeChannel; });
        if (layout.lanes) {
            size_t n = 0, wanted = static_cast<size_t>((io.MousePos.y - origin.y) / layout.rowHeight);
            target = std::find_if(channels.begin(), channels.end(),
                                  [&](const auto& c) { return c.visible && n++ == wanted; });
        }
        if (target != channels.end()) {
            const int group = target->timeGroup;
            const auto id = target->id;
            for (auto& c : channels)
                if (c.id == id || (group >= 0 && c.timeGroup == group)) {
                    const float local = t - c.offset;
                    if (local >= 0 && local <= c.end) {
                        SetScalarCurveKey(*c.curve, local, c.curve->Evaluate(local));
                        changed = true;
                    }
                }
            edit.selection.clear();
            view.gesture = Gesture::None;
            edit.End();
        }
    }
    if (ImGui::BeginPopupContextItem("curveContext")) {
        if (hit && !hit->key.channel.empty())
            edit.Select(hit->key);
        for (const auto& preset : kCurveKeyPresetNames)
            if (ImGui::MenuItem(preset.name, nullptr, false, !edit.selection.empty()))
                changed |= edit.ApplyPreset(channels, preset.value);
        ImGui::Separator();
        if (ImGui::MenuItem("复制", "Ctrl+C"))
            edit.Copy(channels);
        if (ImGui::MenuItem("粘贴", "Ctrl+V"))
            changed |= edit.Paste(channels, playhead, view.activeChannel);
        if (ImGui::MenuItem("删除", "Delete", false, !edit.selection.empty()))
            changed |= edit.Delete(channels);
        ImGui::EndPopup();
    }
    changed |= CurveEditorShortcuts(channels, playhead, view, focused);
    return changed;
}
} // namespace HIKARI::EDITOR
