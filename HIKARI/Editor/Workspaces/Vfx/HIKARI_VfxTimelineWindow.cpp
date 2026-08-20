#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr float kTimelineFramesPerSecond = 60.0f;
        constexpr float kMinimumTimelineRange =
            1.0f / kTimelineFramesPerSecond;

        enum TimelineDragMode : int {
            TimelineDragNone = 0,
            TimelineDragPlayhead,
            TimelineDragLoopStart,
            TimelineDragLoopEnd,
        };

        float ChooseMajorTickStep(float duration, float width) {
            const float desiredTickCount =
                (std::max)(width / 90.0f, 1.0f);
            const float raw = duration / desiredTickCount;
            const float magnitude = std::pow(
                10.0f,
                std::floor(std::log10((std::max)(raw, 0.0001f))));
            const float normalized = raw / magnitude;
            const float nice = normalized <= 1.0f
                ? 1.0f
                : normalized <= 2.0f
                    ? 2.0f
                    : normalized <= 5.0f
                        ? 5.0f
                        : 10.0f;
            return (std::max)(nice * magnitude, kMinimumTimelineRange);
        }

        void DrawTimeLabel(
            ImDrawList& drawList,
            float x,
            float y,
            float seconds,
            ImU32 color) {

            char label[32]{};
            std::snprintf(label, sizeof(label), "%.2fs", seconds);
            const ImVec2 size = ImGui::CalcTextSize(label);
            drawList.AddText({ x - size.x * 0.5f, y }, color, label);
        }

        uint64_t TimelineMergeGroup(
            std::string_view targetId,
            std::string_view propertyId) noexcept {

            uint64_t hash = 1469598103934665603ull;
            const auto append = [&hash](std::string_view text) {
                for (const unsigned char character : text) {
                    hash ^= character;
                    hash *= 1099511628211ull;
                }
            };
            append(targetId);
            append("/");
            append(propertyId);
            return hash != 0u ? hash : 1u;
        }
#endif
    } // namespace

    void VfxWorkspaceController::DrawTimeline() {
#if defined(HIKARI_WITH_EDITOR)
        if (!document_.IsOpen()) {
            ImGui::TextDisabled("Open or create a VFX asset to use the timeline.");
            return;
        }

        NormalizePreviewTimelineRange();
        const float duration = GetPreviewTimelineDuration();

        if (!previewPlaying_) {
            if (ImGui::Button("Play", { 58.0f, 26.0f })) {
                (void)PlayPreview(statusMessage_);
            }
        } else if (ImGui::Button(
                previewPaused_ ? "Resume" : "Pause",
                { 62.0f, 26.0f })) {
            PausePreview(!previewPaused_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart", { 64.0f, 26.0f })) {
            (void)RestartPreview(statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!previewPlaying_);
        if (ImGui::Button("Stop", { 54.0f, 26.0f })) {
            StopPreview();
            statusMessage_ = "VFX preview stopped";
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0f);
        float playbackSpeed = previewPlaybackSpeed_;
        if (ImGui::DragFloat(
                "Speed",
                &playbackSpeed,
                0.05f,
                0.1f,
                4.0f,
                "%.2fx")) {
            SetPreviewPlaybackSpeed(playbackSpeed);
        }

        ImGui::SameLine();
        bool loopEnabled = previewLoopEnabled_;
        if (ImGui::Checkbox("Loop", &loopEnabled)) {
            SetPreviewLoopEnabled(loopEnabled);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(82.0f);
        float loopStart = previewLoopStartSeconds_;
        if (ImGui::DragFloat(
                "Start",
                &loopStart,
                kMinimumTimelineRange,
                0.0f,
                duration,
                "%.2f s")) {
            SetPreviewLoopRange(loopStart, previewLoopEndSeconds_);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(82.0f);
        float loopEnd = previewLoopEndSeconds_;
        if (ImGui::DragFloat(
                "End",
                &loopEnd,
                kMinimumTimelineRange,
                0.0f,
                duration,
                "%.2f s")) {
            SetPreviewLoopRange(previewLoopStartSeconds_, loopEnd);
        }

        ImGui::SameLine();
        ImGui::TextDisabled(
            "%.2f / %.2f s   frame %u",
            previewElapsedSeconds_,
            duration,
            static_cast<unsigned int>(std::lround(
                previewElapsedSeconds_ * kTimelineFramesPerSecond)));

        const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize{
            (std::max)(ImGui::GetContentRegionAvail().x, 240.0f),
            94.0f
        };
        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        drawList.AddRectFilled(
            canvasOrigin,
            { canvasOrigin.x + canvasSize.x,
              canvasOrigin.y + canvasSize.y },
            IM_COL32(13, 17, 24, 255),
            4.0f);

        const float rulerTop = canvasOrigin.y + 22.0f;
        const float rulerBottom = canvasOrigin.y + canvasSize.y - 12.0f;
        const auto timeToX = [&](float seconds) {
            return canvasOrigin.x +
                std::clamp(seconds / duration, 0.0f, 1.0f) *
                    canvasSize.x;
        };
        const auto xToTime = [&](float x) {
            const float normalized = std::clamp(
                (x - canvasOrigin.x) / canvasSize.x,
                0.0f,
                1.0f);
            return normalized * duration;
        };

        const float loopStartX = timeToX(previewLoopStartSeconds_);
        const float loopEndX = timeToX(previewLoopEndSeconds_);
        drawList.AddRectFilled(
            { loopStartX, rulerTop },
            { loopEndX, rulerBottom },
            previewLoopEnabled_
                ? IM_COL32(66, 111, 145, 54)
                : IM_COL32(89, 98, 110, 34));

        const float majorStep = ChooseMajorTickStep(
            duration, canvasSize.x);
        const float minorStep = majorStep / 5.0f;
        const int tickCount = (std::min)(
            static_cast<int>(std::ceil(duration / minorStep)),
            1000);
        for (int index = 0; index <= tickCount; ++index) {
            const float time = (std::min)(
                static_cast<float>(index) * minorStep,
                duration);
            const bool major = index % 5 == 0;
            const float x = timeToX(time);
            drawList.AddLine(
                { x, rulerTop },
                { x, rulerTop + (major ? 15.0f : 7.0f) },
                major
                    ? IM_COL32(160, 174, 191, 190)
                    : IM_COL32(103, 117, 134, 120),
                major ? 1.25f : 1.0f);
            if (major) {
                DrawTimeLabel(
                    drawList,
                    x,
                    canvasOrigin.y + 2.0f,
                    time,
                    IM_COL32(179, 191, 207, 220));
            }
        }

        drawList.AddLine(
            { loopStartX, rulerTop },
            { loopStartX, rulerBottom },
            IM_COL32(91, 205, 255, 230),
            2.0f);
        drawList.AddLine(
            { loopEndX, rulerTop },
            { loopEndX, rulerBottom },
            IM_COL32(91, 205, 255, 230),
            2.0f);

        const float playheadX = timeToX(previewElapsedSeconds_);
        drawList.AddTriangleFilled(
            { playheadX, rulerTop - 1.0f },
            { playheadX - 5.0f, rulerTop - 9.0f },
            { playheadX + 5.0f, rulerTop - 9.0f },
            IM_COL32(255, 210, 92, 255));
        drawList.AddLine(
            { playheadX, rulerTop },
            { playheadX, rulerBottom },
            IM_COL32(255, 210, 92, 255),
            2.0f);

        ImGui::InvisibleButton(
            "##VfxTimelineRuler",
            canvasSize,
            ImGuiButtonFlags_MouseButtonLeft);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated()) {
            const float startDistance = std::abs(mouse.x - loopStartX);
            const float endDistance = std::abs(mouse.x - loopEndX);
            if (startDistance <= 7.0f && startDistance <= endDistance) {
                previewTimelineDragMode_ = TimelineDragLoopStart;
            } else if (endDistance <= 7.0f) {
                previewTimelineDragMode_ = TimelineDragLoopEnd;
            } else {
                previewTimelineDragMode_ = TimelineDragPlayhead;
            }
        }
        if (ImGui::IsItemActive()) {
            const float time = xToTime(mouse.x);
            switch (previewTimelineDragMode_) {
            case TimelineDragLoopStart:
                SetPreviewLoopRange(time, previewLoopEndSeconds_);
                break;
            case TimelineDragLoopEnd:
                SetPreviewLoopRange(previewLoopStartSeconds_, time);
                break;
            case TimelineDragPlayhead:
                if (std::abs(time - previewElapsedSeconds_) >=
                    kMinimumTimelineRange * 0.5f) {
                    (void)SeekPreview(time, statusMessage_);
                }
                break;
            default:
                break;
            }
        }
        if (ImGui::IsItemDeactivated()) {
            previewTimelineDragMode_ = TimelineDragNone;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip(
                "%.2f s  |  frame %u",
                xToTime(mouse.x),
                static_cast<unsigned int>(std::lround(
                    xToTime(mouse.x) * kTimelineFramesPerSecond)));
        }

        // Authoring rows intentionally live beside the preview ruler. They
        // edit the source asset through the same controller transactions used
        // by the Inspector, so seeking the playhead never masquerades as a
        // key edit.
        ImGui::SeparatorText("Emitter activation / burst keys");
        const VFX::VfxAssetData snapshot = document_.Asset();
        for (const VFX::VfxEmitterData& emitter : snapshot.emitters) {
            ImGui::PushID(emitter.id.c_str());
            ImGui::Text("%s", emitter.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", emitter.id.c_str());

            float startDelay = emitter.startDelay;
            ImGui::SetNextItemWidth(128.0f);
            if (ImGui::DragFloat(
                    "Start Delay",
                    &startDelay,
                    0.01f,
                    0.0f,
                    duration,
                    "%.3f s")) {
                std::string message{};
                (void)SetProperty(
                    emitter.id,
                    "startDelay",
                    startDelay,
                    message,
                    TimelineMergeGroup(emitter.id, "startDelay"));
                statusMessage_ = message;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                SealPropertyEdit();
            }
            ImGui::SameLine();
            float activeDuration = emitter.activeDuration;
            const float activeDurationMaximum = (std::max)(
                0.0f, duration - startDelay);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::DragFloat(
                    "Active Duration (0 = end)",
                    &activeDuration,
                    0.01f,
                    0.0f,
                    activeDurationMaximum,
                    "%.3f s")) {
                std::string message{};
                (void)SetProperty(
                    emitter.id,
                    "activeDuration",
                    activeDuration,
                    message,
                    TimelineMergeGroup(emitter.id, "activeDuration"));
                statusMessage_ = message;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                SealPropertyEdit();
            }

            const float localDuration = emitter.activeDuration > 0.0f
                ? emitter.activeDuration
                : (std::max)(0.0f, duration - emitter.startDelay);
            ImGui::TextDisabled(
                "Burst keys are emitter-local (0..%.3f s)",
                localDuration);
            bool removeBurst = false;
            std::string removeBurstId{};
            for (const VFX::VfxBurstKeyData& key :
                 emitter.spawn.burstTimeline) {
                ImGui::PushID(key.id.c_str());
                if (ImGui::Selectable(
                        key.id.c_str(),
                        selection_.kind == VfxWorkspaceSelectionKind::BurstKey &&
                            selection_.targetId == key.id)) {
                    Select({ VfxWorkspaceSelectionKind::BurstKey, key.id,
                        emitter.id });
                }
                ImGui::SameLine();
                float keyTime = key.time;
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::DragFloat(
                        "Time",
                        &keyTime,
                        0.01f,
                        0.0f,
                        localDuration,
                        "%.3f s")) {
                    std::string message{};
                    (void)SetBurstKey(
                        key.id,
                        keyTime,
                        key.count,
                        message,
                        TimelineMergeGroup(key.id, "time"));
                    statusMessage_ = message;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    SealPropertyEdit();
                }
                ImGui::SameLine();
                uint32_t keyCount = key.count;
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::DragScalar(
                        "Count",
                        ImGuiDataType_U32,
                        &keyCount,
                        1.0f,
                        nullptr,
                        &emitter.spawn.maximumParticles)) {
                    std::string message{};
                    (void)SetBurstKey(
                        key.id,
                        key.time,
                        keyCount,
                        message,
                        TimelineMergeGroup(key.id, "count"));
                    statusMessage_ = message;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    SealPropertyEdit();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    removeBurst = true;
                    removeBurstId = key.id;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Burst Key at Playhead")) {
                const float localTime = std::clamp(
                    previewElapsedSeconds_ - emitter.startDelay,
                    0.0f,
                    localDuration);
                std::string keyId{};
                std::string message{};
                if (!AddBurstKey(
                        emitter.id,
                        localTime,
                        1u,
                        {},
                        keyId,
                        message)) {
                    statusMessage_ = message;
                }
            }
            if (removeBurst) {
                std::string message{};
                (void)RemoveBurstKey(removeBurstId, message);
                statusMessage_ = message;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
#endif
    }

} // namespace HIKARI::EDITOR
