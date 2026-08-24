#include "Editor/Workspaces/CombatAction/HIKARI_CombatActionWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr ImVec4 kAnimationColor{ 0.40f, 0.68f, 1.0f, 1.0f };
        constexpr ImVec4 kPhaseColor{ 0.40f, 0.86f, 0.66f, 1.0f };
        constexpr ImVec4 kHitColor{ 1.0f, 0.38f, 0.30f, 1.0f };
        constexpr ImVec4 kCancelColor{ 0.98f, 0.76f, 0.30f, 1.0f };
        constexpr ImVec4 kMotionColor{ 0.62f, 0.50f, 1.0f, 1.0f };
        constexpr ImVec4 kDefenseColor{ 0.30f, 0.82f, 0.92f, 1.0f };
        constexpr ImVec4 kVfxColor{ 1.0f, 0.45f, 0.74f, 1.0f };
        constexpr float kTrackLabelWidth = 122.0f;
        constexpr float kTrackHeight = 31.0f;

        const char* TrackName(CombatActionTrackKind track) noexcept {
            switch (track) {
            case CombatActionTrackKind::Animation: return "Animation";
            case CombatActionTrackKind::Phase: return "Action Phases";
            case CombatActionTrackKind::Hit: return "Hit Detection";
            case CombatActionTrackKind::Cancel: return "Cancel / Buffer";
            case CombatActionTrackKind::Motion: return "Motion / Warp";
            case CombatActionTrackKind::Defense: return "Defense";
            case CombatActionTrackKind::Vfx: return "VFX Cues";
            case CombatActionTrackKind::Presentation: return "Presentation";
            case CombatActionTrackKind::None:
            default: return "Action";
            }
        }

        ImU32 TrackColor(CombatActionTrackKind track, bool selected) {
            ImVec4 color{};
            switch (track) {
            case CombatActionTrackKind::Animation: color = kAnimationColor; break;
            case CombatActionTrackKind::Phase: color = kPhaseColor; break;
            case CombatActionTrackKind::Hit: color = kHitColor; break;
            case CombatActionTrackKind::Cancel: color = kCancelColor; break;
            case CombatActionTrackKind::Motion: color = kMotionColor; break;
            case CombatActionTrackKind::Defense: color = kDefenseColor; break;
            case CombatActionTrackKind::Vfx: color = kVfxColor; break;
            default: color = ImVec4(0.48f, 0.52f, 0.58f, 1.0f); break;
            }
            if (!selected) color.w = 0.78f;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        float SnapTime(float value, uint32_t fps, bool enabled) noexcept {
            if (!enabled || fps == 0u) return value;
            return std::round(value * static_cast<float>(fps)) /
                static_cast<float>(fps);
        }
#endif
    }

    void CombatActionWorkspaceController::DrawTimeline() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Action Timeline###CombatAction/Timeline")) {
            ImGui::End();
            return;
        }
        ACTION::CombatActionDefinition& definition = document_.Definition();
        if (ImGui::Button(playing_ ? "Pause" : "Play")) playing_ = !playing_;
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            playing_ = false;
            currentTimeSeconds_ = 0.0f;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &looping_);
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snapEnabled_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat(
            "Zoom", &timelinePixelsPerSecond_, 100.0f, 900.0f, "%.0f px/s");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(125.0f);
        ImGui::DragFloat(
            "Scroll", &timelineScrollSeconds_, 0.02f, 0.0f,
            (std::max)(0.0f, definition.durationSeconds - 0.1f), "%.2f s");
        ImGui::SameLine();
        ImGui::Text("%5.3f / %5.3f s", currentTimeSeconds_, definition.durationSeconds);

        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize{
            (std::max)(ImGui::GetContentRegionAvail().x, 520.0f),
            kTrackHeight * 8.0f + 34.0f
        };
        ImGui::InvisibleButton(
            "##TimelineCanvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft);
        const ImVec2 canvasMax{ canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y };
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(12, 15, 20, 255), 5.0f);
        draw->AddRect(canvasMin, canvasMax, IM_COL32(55, 62, 74, 255), 5.0f);
        const float timeOriginX = canvasMin.x + kTrackLabelWidth;
        const float rulerY = canvasMin.y + 30.0f;
        draw->AddLine(
            ImVec2(timeOriginX, canvasMin.y),
            ImVec2(timeOriginX, canvasMax.y),
            IM_COL32(78, 86, 102, 255));

        const float visibleStart = timelineScrollSeconds_;
        const float visibleEnd = visibleStart +
            (canvasSize.x - kTrackLabelWidth) / timelinePixelsPerSecond_;
        const float major = timelinePixelsPerSecond_ >= 500.0f ? 0.25f :
            timelinePixelsPerSecond_ >= 250.0f ? 0.5f : 1.0f;
        const float firstTick = std::floor(visibleStart / major) * major;
        for (float time = firstTick; time <= visibleEnd + major; time += major) {
            const float x = timeOriginX +
                (time - visibleStart) * timelinePixelsPerSecond_;
            if (x < timeOriginX) continue;
            draw->AddLine(
                ImVec2(x, canvasMin.y + 17.0f),
                ImVec2(x, canvasMax.y),
                IM_COL32(46, 52, 63, 255));
            char label[32]{};
            std::snprintf(label, sizeof(label), "%.2f", time);
            draw->AddText(
                ImVec2(x + 3.0f, canvasMin.y + 2.0f),
                IM_COL32(150, 160, 176, 255), label);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x >= timeOriginX && mouse.y <= rulerY) {
                currentTimeSeconds_ = std::clamp(
                    visibleStart + (mouse.x - timeOriginX) /
                        timelinePixelsPerSecond_,
                    0.0f, definition.durationSeconds);
                currentTimeSeconds_ = SnapTime(
                    currentTimeSeconds_,
                    definition.displayFramesPerSecond,
                    snapEnabled_);
                playing_ = false;
            }
        }

        const auto rowY = [canvasMin](int row) {
            return canvasMin.y + 30.0f + static_cast<float>(row) * kTrackHeight;
        };
        const std::array tracks{
            CombatActionTrackKind::Animation,
            CombatActionTrackKind::Phase,
            CombatActionTrackKind::Hit,
            CombatActionTrackKind::Cancel,
            CombatActionTrackKind::Motion,
            CombatActionTrackKind::Defense,
            CombatActionTrackKind::Vfx,
            CombatActionTrackKind::Presentation,
        };
        for (int row = 0; row < static_cast<int>(tracks.size()); ++row) {
            const float y = rowY(row);
            if ((row & 1) != 0) {
                draw->AddRectFilled(
                    ImVec2(canvasMin.x + 1.0f, y),
                    ImVec2(canvasMax.x - 1.0f, y + kTrackHeight),
                    IM_COL32(18, 22, 29, 255));
            }
            draw->AddText(
                ImVec2(canvasMin.x + 10.0f, y + 8.0f),
                IM_COL32(195, 203, 216, 255), TrackName(tracks[row]));
            draw->AddLine(
                ImVec2(canvasMin.x, y + kTrackHeight),
                ImVec2(canvasMax.x, y + kTrackHeight),
                IM_COL32(36, 42, 52, 255));
        }

        const auto drawRange = [this, &definition, draw, timeOriginX,
                                visibleStart, canvasMax, &rowY](
                                    CombatActionTrackKind track,
                                    int row,
                                    uint64_t id,
                                    std::string_view label,
                                    float& start,
                                    float& end) {
            const float y = rowY(row) + 4.0f;
            const float x0 = timeOriginX +
                (start - visibleStart) * timelinePixelsPerSecond_;
            const float x1 = timeOriginX +
                (end - visibleStart) * timelinePixelsPerSecond_;
            if (x1 < timeOriginX || x0 > canvasMax.x) return;
            const ImVec2 minimum{ (std::max)(timeOriginX, x0), y };
            const ImVec2 maximum{ (std::min)(canvasMax.x, x1), y + kTrackHeight - 8.0f };
            const bool selected = selection_.track == track && selection_.id == id;
            draw->AddRectFilled(minimum, maximum, TrackColor(track, selected), 4.0f);
            if (selected) {
                draw->AddRect(
                    minimum, maximum, IM_COL32(255, 255, 255, 235), 4.0f, 0, 2.0f);
            }
            if (maximum.x - minimum.x > 28.0f) {
                draw->PushClipRect(minimum, maximum, true);
                draw->AddText(
                    ImVec2(minimum.x + 6.0f, minimum.y + 4.0f),
                    IM_COL32(12, 15, 20, 255),
                    std::string(label).c_str());
                draw->PopClipRect();
            }
            ImGui::SetCursorScreenPos(minimum);
            ImGui::PushID(static_cast<int>(track));
            ImGui::PushID(static_cast<int>(id));
            ImGui::InvisibleButton(
                "##ActionRange", ImVec2(
                    (std::max)(maximum.x - minimum.x, 5.0f),
                    maximum.y - minimum.y));
            if (ImGui::IsItemClicked()) selection_ = { track, id };
            if (ImGui::IsItemActivated()) {
                timelineDragBefore_ = definition;
                const float mouseTime = visibleStart +
                    (ImGui::GetMousePos().x - timeOriginX) /
                        timelinePixelsPerSecond_;
                const float edgeSeconds = 7.0f / timelinePixelsPerSecond_;
                timelineDragMode_ = std::abs(mouseTime - start) <= edgeSeconds
                    ? 1u : std::abs(mouseTime - end) <= edgeSeconds ? 2u : 3u;
                timelineDragGrabOffsetSeconds_ = mouseTime - start;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const float mouseTime = SnapTime(
                    visibleStart +
                        (ImGui::GetMousePos().x - timeOriginX) /
                            timelinePixelsPerSecond_,
                    definition.displayFramesPerSecond,
                    snapEnabled_);
                const float minimumDuration = 1.0f /
                    static_cast<float>((std::max)(1u, definition.displayFramesPerSecond));
                if (timelineDragMode_ == 1u) {
                    start = std::clamp(mouseTime, 0.0f, end - minimumDuration);
                } else if (timelineDragMode_ == 2u) {
                    end = std::clamp(mouseTime, start + minimumDuration,
                        definition.durationSeconds);
                } else {
                    const float duration = end - start;
                    start = std::clamp(
                        mouseTime - timelineDragGrabOffsetSeconds_,
                        0.0f,
                        definition.durationSeconds - duration);
                    end = start + duration;
                }
            }
            if (ImGui::IsItemDeactivated() && timelineDragBefore_) {
                RecordMutation(std::move(*timelineDragBefore_));
                timelineDragBefore_.reset();
                timelineDragMode_ = 0u;
            }
            ImGui::PopID();
            ImGui::PopID();
        };

        for (auto& value : definition.animationSegments) {
            float end = value.startTimeSeconds + value.durationSeconds;
            drawRange(CombatActionTrackKind::Animation, 0, value.id,
                value.clipName, value.startTimeSeconds, end);
            value.durationSeconds = end - value.startTimeSeconds;
        }
        for (auto& value : definition.phases) {
            drawRange(CombatActionTrackKind::Phase, 1, value.id,
                ACTION::ToString(value.kind), value.startTimeSeconds, value.endTimeSeconds);
        }
        for (auto& value : definition.hitWindows) {
            drawRange(CombatActionTrackKind::Hit, 2, value.id,
                value.label, value.startTimeSeconds, value.endTimeSeconds);
        }
        for (auto& value : definition.cancelWindows) {
            drawRange(CombatActionTrackKind::Cancel, 3, value.id,
                value.label, value.startTimeSeconds, value.endTimeSeconds);
        }
        for (auto& value : definition.motionWindows) {
            drawRange(CombatActionTrackKind::Motion, 4, value.id,
                ACTION::ToString(value.mode), value.startTimeSeconds, value.endTimeSeconds);
        }
        for (auto& value : definition.defenseWindows) {
            drawRange(CombatActionTrackKind::Defense, 5, value.id,
                value.invulnerable ? "Invulnerable" :
                    value.superArmor ? "Super Armor" : "Damage Reduction",
                value.startTimeSeconds, value.endTimeSeconds);
        }
        for (auto& value : definition.presentation.vfxCues) {
            const float x = timeOriginX +
                (value.timeSeconds - visibleStart) * timelinePixelsPerSecond_;
            const float y = rowY(6) + kTrackHeight * 0.5f;
            if (x < timeOriginX || x > canvasMax.x) continue;
            const bool selected = selection_.track == CombatActionTrackKind::Vfx &&
                selection_.id == value.id;
            draw->AddCircleFilled(
                ImVec2(x, y), selected ? 8.0f : 6.0f,
                TrackColor(CombatActionTrackKind::Vfx, selected));
            ImGui::SetCursorScreenPos(ImVec2(x - 9.0f, y - 9.0f));
            ImGui::PushID(static_cast<int>(value.id));
            ImGui::InvisibleButton("##VfxCue", ImVec2(18.0f, 18.0f));
            if (ImGui::IsItemClicked()) {
                selection_ = { CombatActionTrackKind::Vfx, value.id };
            }
            if (ImGui::IsItemActivated()) timelineDragBefore_ = definition;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                value.timeSeconds = std::clamp(
                    SnapTime(
                        visibleStart +
                            (ImGui::GetMousePos().x - timeOriginX) /
                                timelinePixelsPerSecond_,
                        definition.displayFramesPerSecond,
                        snapEnabled_),
                    0.0f, definition.durationSeconds);
            }
            if (ImGui::IsItemDeactivated() && timelineDragBefore_) {
                RecordMutation(std::move(*timelineDragBefore_));
                timelineDragBefore_.reset();
            }
            ImGui::PopID();
        }
        const float playheadX = timeOriginX +
            (currentTimeSeconds_ - visibleStart) * timelinePixelsPerSecond_;
        if (playheadX >= timeOriginX && playheadX <= canvasMax.x) {
            draw->AddLine(
                ImVec2(playheadX, canvasMin.y),
                ImVec2(playheadX, canvasMax.y),
                IM_COL32(255, 98, 60, 255), 2.0f);
            draw->AddTriangleFilled(
                ImVec2(playheadX - 5.0f, canvasMin.y),
                ImVec2(playheadX + 5.0f, canvasMin.y),
                ImVec2(playheadX, canvasMin.y + 8.0f),
                IM_COL32(255, 98, 60, 255));
        }
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x, canvasMax.y + 4.0f));
        ImGui::End();
#endif
    }

} // namespace HIKARI::EDITOR
