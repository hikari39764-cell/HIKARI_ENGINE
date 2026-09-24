#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
uint64_t NextTrack(const UiAnimationClip& clip) {
    uint64_t id = 1;
    for (const auto& t : clip.tracks)
        id = std::max(id, t.id + 1);
    return id;
}
} // namespace
void UiAnimationEditor::DrawTimeline(UiAnimationAsset&, UiAnimationClip& clip, bool& changed) {
    if (IconTextButton(EditorGlyph::Add, L("uianim.addTrack"), "addTrack", EditorButtonTone::Quiet))
        ImGui::OpenPopup("motionProperty");
    if (ImGui::BeginPopup("motionProperty")) {
        static char filter[96]{};
        ImGui::InputTextWithHint("##filter", L("uix.search"), filter, sizeof(filter));
        const auto needle = TEXT::ToLowerAsciiCopy(filter);
        for (const auto& p : UiMotionProperties()) {
            const auto label = TEXT::Localize(p.label);
            if (!needle.empty() && TEXT::ToLowerAsciiCopy(label + p.id).find(needle) == std::string::npos)
                continue;
            if (ImGui::Selectable(label.c_str())) {
                UiAnimationTrack t;
                t.id = NextTrack(clip);
                t.property = p.property;
                t.curve.value = p.initial;
                // 同じチャンネルがあれば選択し、重複を作らない。
                auto found = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const auto& track) {
                    return track.slot == "self" && track.property == p.property;
                });
                if (found == clip.tracks.end()) {
                    track_ = t.id;
                    clip.tracks.push_back(t);
                    changed = true;
                } else
                    track_ = found->id;
                curveView_ = {};
                selectedKeys_.clear();
            }
        }
        ImGui::EndPopup();
    }
    auto active = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const auto& t) { return t.id == track_; });
    ImGui::SameLine();
    ImGui::BeginDisabled(active == clip.tracks.end());
    if (IconButton(EditorGlyph::Delete, "deleteTrack", EditorButtonTone::Quiet, {26, 26}, L("uianim.deleteTrack"))) {
        clip.tracks.erase(active);
        active = clip.tracks.end();
        selectedKeys_.clear();
        changed = true;
        track_ = 0;
    }
    ImGui::EndDisabled();
    if (active != clip.tracks.end()) {
        char slot[128]{};
        std::memcpy(slot, active->slot.data(), std::min(active->slot.size(), sizeof(slot) - 1));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText(L("uianim.slot"), slot, sizeof(slot))) {
            active->slot = slot;
            changed = true;
        }
        ImGui::SameLine();
        bool addKey = IconButton(EditorGlyph::Keyframe, "addKey", EditorButtonTone::Quiet, {26, 26}, L("cine.addKey"));
        auto selected = std::find_if(selectedKeys_.begin(), selectedKeys_.end(), [&](const auto& key) {
            return key.first == active->id && key.second < active->curve.keys.size();
        });
        float value = selected != selectedKeys_.end() ? active->curve.keys[selected->second].value
                                                      : active->curve.Evaluate(time_);
        const auto& property = UiMotionProperties()[static_cast<size_t>(active->property)];
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::BeginDisabled(!autoKey_ && selected == selectedKeys_.end() && !active->curve.keys.empty());
        if (ImGui::DragFloat("##Value", &value, .01f, property.minimum, property.maximum, "%.3f")) {
            if (autoKey_)
                addKey = true;
            else if (selected != selectedKeys_.end())
                active->curve.keys[selected->second].value = value;
            else
                active->curve.value = value;
            changed = true;
        }
        ImGui::EndDisabled();
        if (addKey) {
            SetScalarCurveKey(active->curve, time_, value);
            changed = true;
        }
    }
    if (curve_ && active != clip.tracks.end()) {
        changed |= DrawScalarCurve(active->curve, clip.duration, time_, curveView_,
                                   std::max(110.f, ImGui::GetContentRegionAvail().y - 50));
        return;
    }
    const bool shortcut =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    if (shortcut && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        clipboard_.clear();
        float start = std::numeric_limits<float>::max();
        for (const auto& t : clip.tracks)
            for (size_t i = 0; i < t.curve.keys.size(); ++i)
                if (selectedKeys_.contains({t.id, i}))
                    start = std::min(start, t.curve.keys[i].time);
        for (const auto& t : clip.tracks)
            for (size_t i = 0; i < t.curve.keys.size(); ++i)
                if (selectedKeys_.contains({t.id, i})) {
                    auto k = t.curve.keys[i];
                    k.time -= start;
                    clipboard_.push_back({t.slot, t.property, k});
                }
    }
    if (shortcut && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        for (const auto& copy : clipboard_) {
            auto target = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const auto& t) {
                return t.slot == copy.slot && t.property == copy.property;
            });
            if (target == clip.tracks.end()) {
                UiAnimationTrack t;
                t.id = NextTrack(clip);
                t.slot = copy.slot;
                t.property = copy.property;
                t.curve.value = UiMotionProperties()[static_cast<size_t>(copy.property)].initial;
                clip.tracks.push_back(t);
                target = std::prev(clip.tracks.end());
            }
            auto key = copy.key;
            key.time = std::min(clip.duration, key.time + time_);
            SetScalarCurveKey(target->curve, key.time, key.value);
            for (auto& k : target->curve.keys)
                if (std::abs(k.time - key.time) < .0001f)
                    k = key;
            changed = true;
        }
        selectedKeys_.clear();
    }
    if (ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, shortcut, false) &&
        !selectedKeys_.empty()) {
        for (auto& t : clip.tracks)
            for (size_t i = t.curve.keys.size(); i-- > 0;)
                if (selectedKeys_.contains({t.id, i})) {
                    t.curve.value = t.curve.keys[i].value;
                    t.curve.keys.erase(t.curve.keys.begin() + i);
                    changed = true;
                }
        selectedKeys_.clear();
    }
    ImGui::BeginChild("motionRows", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
    const float labelWidth = 185, rowHeight = 28;
    const auto origin = ImGui::GetCursorScreenPos();
    const float width = std::max(180.f, ImGui::GetContentRegionAvail().x - labelWidth - 14);
    const float timelineX = origin.x + labelWidth;
    auto* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i <= 10; ++i) {
        char text[32]{};
        std::snprintf(text, sizeof(text), "%.2f", clip.duration * i / 10);
        draw->AddText({timelineX + width * i / 10, origin.y}, IM_COL32(132, 147, 166, 255), text);
    }
    ImGui::Dummy({labelWidth + width, rowHeight});
    for (auto& t : clip.tracks) {
        ImGui::PushID(static_cast<int>(t.id));
        const auto row = ImGui::GetCursorScreenPos();
        const auto& prop = UiMotionProperties()[static_cast<size_t>(t.property)];
        const auto label = t.slot + " · " + TEXT::Localize(prop.label);
        if (ImGui::Selectable(label.c_str(), track_ == t.id, 0, {labelWidth - 8, rowHeight - 2})) {
            track_ = t.id;
            curveView_ = {};
        }
        draw->AddRectFilled({timelineX, row.y}, {timelineX + width, row.y + rowHeight - 1},
                            track_ == t.id ? IM_COL32(28, 42, 54, 255) : IM_COL32(18, 26, 35, 255));
        for (int i = 0; i <= 10; ++i)
            draw->AddLine({timelineX + width * i / 10, row.y}, {timelineX + width * i / 10, row.y + rowHeight},
                          IM_COL32(50, 61, 73, 130));
        bool keyHovered = false;
        for (size_t i = 0; i < t.curve.keys.size(); ++i) {
            const auto& key = t.curve.keys[i];
            const ImVec2 p{timelineX + key.time / clip.duration * width, row.y + rowHeight * .5f};
            ImGui::SetCursorScreenPos({p.x - 7, p.y - 7});
            ImGui::PushID(static_cast<int>(i));
            ImGui::InvisibleButton("key", {14, 14});
            keyHovered |= ImGui::IsItemHovered();
            if (ImGui::IsItemActivated()) {
                if (!ImGui::GetIO().KeyCtrl && !selectedKeys_.contains({t.id, i}))
                    selectedKeys_.clear();
                if (ImGui::GetIO().KeyCtrl && selectedKeys_.contains({t.id, i}))
                    selectedKeys_.erase({t.id, i});
                else
                    selectedKeys_.insert({t.id, i});
                track_ = t.id;
                drag_ = KeyDrag{clip, ImGui::GetIO().MousePos.x};
            }
            const bool selected = selectedKeys_.contains({t.id, i});
            draw->AddQuadFilled({p.x, p.y - 5}, {p.x + 5, p.y}, {p.x, p.y + 5}, {p.x - 5, p.y},
                                selected ? IM_COL32(255, 197, 95, 255) : IM_COL32(114, 216, 227, 255));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.3f s · %.3f", key.time, key.value);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({timelineX, row.y});
        ImGui::InvisibleButton("row", {width, rowHeight});
        if (!keyHovered && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            time_ = std::clamp((ImGui::GetIO().MousePos.x - timelineX) / width * clip.duration, 0.f, clip.duration);
            if (snap_ && !ImGui::GetIO().KeyAlt)
                time_ = std::clamp(std::round(time_ * 60) / 60, 0.f, clip.duration);
            playing_ = false;
            selectedKeys_.clear();
            track_ = t.id;
            if (ImGui::IsMouseDoubleClicked(0)) {
                SetScalarCurveKey(t.curve, time_, t.curve.Evaluate(time_));
                changed = true;
            }
        }
        const float cursor = timelineX + time_ / clip.duration * width;
        draw->AddLine({cursor, row.y}, {cursor, row.y + rowHeight}, IM_COL32(245, 206, 123, 230));
        ImGui::SetCursorScreenPos({origin.x, row.y + rowHeight});
        ImGui::PopID();
    }
    if (drag_ && ImGui::IsMouseDragging(0)) {
        float delta = (ImGui::GetIO().MousePos.x - drag_->x) / width * clip.duration;
        if (snap_ && !ImGui::GetIO().KeyAlt)
            delta = std::round(delta * 60) / 60;
        float low = -clip.duration, high = clip.duration;
        for (const auto& t : drag_->before.tracks)
            for (size_t i = 0; i < t.curve.keys.size(); ++i)
                if (selectedKeys_.contains({t.id, i})) {
                    const float time = t.curve.keys[i].time;
                    low = std::max(low, -time);
                    high = std::min(high, clip.duration - time);
                    if (i && !selectedKeys_.contains({t.id, i - 1}))
                        low = std::max(low, t.curve.keys[i - 1].time + .0001f - time);
                    if (i + 1 < t.curve.keys.size() && !selectedKeys_.contains({t.id, i + 1}))
                        high = std::min(high, t.curve.keys[i + 1].time - .0001f - time);
                }
        delta = std::clamp(delta, low, std::max(low, high));
        for (auto& t : clip.tracks)
            for (const auto& before : drag_->before.tracks)
                if (before.id == t.id)
                    for (size_t i = 0; i < t.curve.keys.size() && i < before.curve.keys.size(); ++i)
                        if (selectedKeys_.contains({t.id, i})) {
                            t.curve.keys[i].time = before.curve.keys[i].time + delta;
                            changed = true;
                        }
    }
    if (ImGui::IsMouseReleased(0))
        drag_.reset();
    ImGui::EndChild();
}
} // namespace HIKARI::EDITOR
