#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "imgui.h"
#include "Editor/HIKARI_EditorContext.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::Localize(key).c_str(); }
} // namespace
void UiAnimationEditor::DrawTimeline(DocumentSceneBase& scene, EditorContext& context,
                                     EditorOperationRegistry& operations, UiAnimationAsset&, UiAnimationClip& clip,
                                     bool& changed) {
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
                UiAnimationTrack target;
                if (!AssignTarget(scene, context.selection.GetActiveObjectId(), operations, target))
                    continue;
                const auto count = clip.tracks.size();
                track_ = EnsureTrack(clip, target.slot, p.property).id;
                changed |= clip.tracks.size() != count;
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
        ImGui::SameLine();
        ImGui::TextUnformatted(TargetName(scene, active->slot).c_str());
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
            auto* target = &EnsureTrack(clip, copy.slot, copy.property);
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
    active = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const auto& t) { return t.id == track_; });
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat(L("uianim.zoom"), &timelineZoom_, 1, 12, "%.1fx");
    ImGui::SameLine();
    if (ImGui::SmallButton(L("uix.fit")))
        timelineZoom_ = 1;
    ImGui::SameLine();
    if (ImGui::Button(L("uianim.batch")))
        ImGui::OpenPopup("UiMotionBatch");
    if (ImGui::BeginPopup("UiMotionBatch")) {
        ImGui::TextWrapped("%s", L("uianim.batchHelp"));
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat(L("uianim.stagger"), &staggerSeconds_, .01f, 0, 10, "%.2f s");
        ImGui::BeginDisabled(active == clip.tracks.end() || context.selection.GetSelectedObjectCount() == 0);
        if (ImGui::Button(L("uianim.copyMotion"))) {
            const std::string sourceSlot = active->slot;
            std::vector<UiAnimationTrack> source;
            for (const auto& t : clip.tracks)
                if (t.slot == sourceSlot)
                    source.push_back(t);
            size_t index = 0;
            const auto targetIds = context.selection.GetSelectedObjectIds();
            for (const auto id : targetIds) {
                UiAnimationTrack target;
                if (!AssignTarget(scene, id, operations, target))
                    break;
                const float delay = staggerSeconds_ * static_cast<float>(index++);
                if (target.slot == sourceSlot)
                    continue;
                for (auto copy : source) {
                    copy.slot = target.slot;
                    for (auto& key : copy.curve.keys)
                        key.time += delay;
                    if (!copy.curve.keys.empty())
                        clip.duration = std::max(clip.duration, copy.curve.keys.back().time);
                    auto& destination = EnsureTrack(clip, copy.slot, copy.property);
                    copy.id = destination.id;
                    destination = std::move(copy);
                }
                changed = true;
            }
            active =
                std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const auto& t) { return t.id == track_; });
            selectedKeys_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::BeginChild("motionRows", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
    const float labelWidth = 185, rowHeight = 28;
    const auto origin = ImGui::GetCursorScreenPos();
    const float width = std::max(180.f, ImGui::GetContentRegionAvail().x - labelWidth - 14) * timelineZoom_;
    const float timelineX = origin.x + labelWidth;
    auto* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i <= 10; ++i) {
        char text[32]{};
        std::snprintf(text, sizeof(text), "%.2f", clip.duration * i / 10);
        draw->AddText({timelineX + width * i / 10, origin.y}, IM_COL32(132, 147, 166, 255), text);
    }
    ImGui::Dummy({labelWidth + width, rowHeight});
    struct Marker {
        uint64_t track;
        size_t key;
        ImVec2 position;
    };
    std::vector<Marker> markers;
    std::vector<UiAnimationTrack*> rows;
    for (auto& t : clip.tracks)
        rows.push_back(&t);
    std::stable_sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) { return a->slot < b->slot; });
    std::string group;
    bool expanded = true;
    for (auto* rowTrack : rows) {
        auto& t = *rowTrack;
        if (group != t.slot) {
            group = t.slot;
            ImGui::SetCursorScreenPos({origin.x, ImGui::GetCursorScreenPos().y});
            expanded = ImGui::TreeNodeEx((TargetName(scene, t.slot) + "##" + t.slot).c_str(),
                                         ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        }
        if (!expanded)
            continue;
        ImGui::PushID(static_cast<int>(t.id));
        const auto row = ImGui::GetCursorScreenPos();
        const auto& prop = UiMotionProperties()[static_cast<size_t>(t.property)];
        const auto label = TEXT::Localize(prop.label);
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
            markers.push_back({t.id, i, p});
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
        if (!keyHovered && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyShift) {
            keyMarquee_ = MATH::Vec2{ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y};
            marqueeBefore_ = selectedKeys_;
        }
        if (!keyHovered && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && !ImGui::GetIO().KeyShift) {
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
    if (keyMarquee_) {
        const auto mouse = ImGui::GetIO().MousePos;
        const ImVec2 low{std::min(keyMarquee_->x, mouse.x), std::min(keyMarquee_->y, mouse.y)};
        const ImVec2 high{std::max(keyMarquee_->x, mouse.x), std::max(keyMarquee_->y, mouse.y)};
        selectedKeys_ = ImGui::GetIO().KeyCtrl ? marqueeBefore_ : decltype(selectedKeys_){};
        for (const auto& marker : markers)
            if (marker.position.x >= low.x && marker.position.x <= high.x && marker.position.y >= low.y &&
                marker.position.y <= high.y)
                selectedKeys_.insert({marker.track, marker.key});
        draw->AddRectFilled(low, high, IM_COL32(50, 210, 221, 30));
        draw->AddRect(low, high, IM_COL32(50, 210, 221, 220));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            selectedKeys_ = marqueeBefore_;
            keyMarquee_.reset();
        }
        if (ImGui::IsMouseReleased(0))
            keyMarquee_.reset();
    }
    if (drag_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        clip = drag_->before;
        changed = true;
        drag_.reset();
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
