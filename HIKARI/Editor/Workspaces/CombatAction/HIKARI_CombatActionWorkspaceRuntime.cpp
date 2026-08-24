#include "Editor/Workspaces/CombatAction/HIKARI_CombatActionWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Gameplay/Action/HIKARI_CombatActionEvaluator.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        template<class T>
        const T* FindById(const std::vector<T>& values, uint64_t id) {
            const auto found = std::find_if(
                values.begin(), values.end(),
                [id](const T& value) { return value.id == id; });
            return found == values.end() ? nullptr : &*found;
        }

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
    }

    void CombatActionWorkspaceController::DrawDiagnostics() const {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Diagnostics###CombatAction/Diagnostics")) {
            ImGui::End();
            return;
        }
        const auto diagnostics = ACTION::ValidateCombatAction(
            document_.Definition());
        if (diagnostics.empty()) {
            ImGui::TextColored(
                ImVec4(0.45f, 0.90f, 0.62f, 1.0f),
                "Ready for runtime consumption");
            ImGui::TextDisabled(
                "No structural timing or asset errors were found.");
        }
        for (const auto& diagnostic : diagnostics) {
            ImVec4 color{ 0.58f, 0.68f, 0.82f, 1.0f };
            const char* prefix = "INFO";
            if (diagnostic.severity ==
                    ACTION::CombatActionDiagnosticSeverity::Warning) {
                color = ImVec4(1.0f, 0.72f, 0.28f, 1.0f);
                prefix = "WARN";
            } else if (diagnostic.severity ==
                    ACTION::CombatActionDiagnosticSeverity::Error) {
                color = ImVec4(1.0f, 0.36f, 0.32f, 1.0f);
                prefix = "ERROR";
            }
            ImGui::TextColored(
                color, "%s  %s", prefix, diagnostic.path.c_str());
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
            ImGui::Separator();
        }
        ImGui::End();
#endif
    }

    void CombatActionWorkspaceController::AdvancePreview(
        float unscaledDeltaTime) {
        if (!playing_) return;
        const float duration = document_.Definition().durationSeconds;
        currentTimeSeconds_ += (std::max)(0.0f, unscaledDeltaTime);
        if (currentTimeSeconds_ < duration) return;
        if (looping_) {
            currentTimeSeconds_ = duration > 0.0f
                ? std::fmod(currentTimeSeconds_, duration) : 0.0f;
        } else {
            currentTimeSeconds_ = duration;
            playing_ = false;
        }
    }

    void CombatActionWorkspaceController::ApplyPreviewPose(
        DocumentSceneBase& scene,
        EditorContext& context) {
        if (scene.IsRuntimePlayActive()) return;
        if (previewTargetObjectId_.value == 0u) {
            previewTargetObjectId_ = context.selection.GetActiveObjectId();
        }
        GameObject* object = scene.GetWorld().FindObject(previewTargetObjectId_);
        AnimatorComponent* animator = object != nullptr
            ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (animator == nullptr) return;
        if (!previewSnapshot_.valid ||
            previewSnapshot_.objectId != previewTargetObjectId_) {
            RestorePreviewPose(scene);
            previewSnapshot_.objectId = previewTargetObjectId_;
            previewSnapshot_.clip = animator->GetClip();
            previewSnapshot_.timeSeconds = animator->GetTime();
            previewSnapshot_.speed = animator->GetSpeed();
            previewSnapshot_.loop = animator->GetLoop();
            previewSnapshot_.playing = animator->IsPlaying();
            previewSnapshot_.valid = true;
        }
        const ACTION::CombatActionEvaluation evaluation =
            ACTION::EvaluateCombatAction(
                document_.Definition(), currentTimeSeconds_);
        if (evaluation.animation == nullptr ||
            evaluation.animation->clipName.empty()) return;
        const auto& segment = *evaluation.animation;
        const float localTime = segment.sourceStartSeconds +
            (currentTimeSeconds_ - segment.startTimeSeconds) *
                segment.playbackRate;
        if (animator->GetClip() != segment.clipName) {
            animator->SetClip(segment.clipName);
        }
        animator->SetLoop(segment.loop);
        animator->SetSpeed(segment.playbackRate);
        animator->SetTime((std::max)(0.0f, localTime));
        animator->SetPlaying(false);
    }

    void CombatActionWorkspaceController::RestorePreviewPose(
        DocumentSceneBase& scene) {
        if (!previewSnapshot_.valid) return;
        GameObject* object = scene.GetWorld().FindObject(
            previewSnapshot_.objectId);
        AnimatorComponent* animator = object != nullptr
            ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (animator != nullptr) {
            animator->SetClip(previewSnapshot_.clip);
            animator->SetLoop(previewSnapshot_.loop);
            animator->SetSpeed(previewSnapshot_.speed);
            animator->SetTime(previewSnapshot_.timeSeconds);
            animator->SetPlaying(previewSnapshot_.playing);
        }
        previewSnapshot_ = {};
    }

    void CombatActionWorkspaceController::NormalizeSelection() noexcept {
        const auto& definition = document_.Definition();
        bool valid = selection_.id == 0u;
        switch (selection_.track) {
        case CombatActionTrackKind::Animation:
            valid |= FindById(
                definition.animationSegments, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Phase:
            valid |= FindById(definition.phases, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Hit:
            valid |= FindById(definition.hitWindows, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Cancel:
            valid |= FindById(
                definition.cancelWindows, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Motion:
            valid |= FindById(
                definition.motionWindows, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Defense:
            valid |= FindById(
                definition.defenseWindows, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Vfx:
            valid |= FindById(
                definition.presentation.vfxCues, selection_.id) != nullptr;
            break;
        case CombatActionTrackKind::Presentation:
        case CombatActionTrackKind::None:
            valid = true;
            break;
        }
        if (!valid) selection_ = {};
    }

    void CombatActionWorkspaceController::DeleteSelection() {
        ACTION::CombatActionDefinition before = document_.Definition();
        auto& definition = document_.Definition();
        const auto eraseById = [this](auto& values) {
            values.erase(
                std::remove_if(
                    values.begin(), values.end(),
                    [this](const auto& value) {
                        return value.id == selection_.id;
                    }),
                values.end());
        };
        switch (selection_.track) {
        case CombatActionTrackKind::Animation:
            eraseById(definition.animationSegments); break;
        case CombatActionTrackKind::Phase:
            eraseById(definition.phases); break;
        case CombatActionTrackKind::Hit:
            eraseById(definition.hitWindows); break;
        case CombatActionTrackKind::Cancel:
            eraseById(definition.cancelWindows); break;
        case CombatActionTrackKind::Motion:
            eraseById(definition.motionWindows); break;
        case CombatActionTrackKind::Defense:
            eraseById(definition.defenseWindows); break;
        case CombatActionTrackKind::Vfx:
            eraseById(definition.presentation.vfxCues); break;
        default: return;
        }
        selection_.id = 0u;
        RecordMutation(std::move(before));
        statusMessage_ = "Timeline item deleted";
    }

    void CombatActionWorkspaceController::AddItem(
        CombatActionTrackKind track) {
        ACTION::CombatActionDefinition before = document_.Definition();
        auto& definition = document_.Definition();
        const uint64_t id = ACTION::AllocateCombatActionItemId(definition);
        const float frame = 1.0f / static_cast<float>(
            (std::max)(1u, definition.displayFramesPerSecond));
        const float start = std::clamp(
            currentTimeSeconds_, 0.0f,
            (std::max)(0.0f, definition.durationSeconds - frame));
        const float end = (std::min)(
            definition.durationSeconds,
            start + (std::max)(frame, definition.durationSeconds * 0.18f));
        switch (track) {
        case CombatActionTrackKind::Animation: {
            ACTION::CombatActionAnimationSegment value{};
            value.id = id;
            value.clipName = "AnimationClip";
            value.startTimeSeconds = start;
            value.durationSeconds = end - start;
            definition.animationSegments.push_back(std::move(value));
        } break;
        case CombatActionTrackKind::Phase:
            definition.phases.push_back({
                id, ACTION::CombatActionPhaseKind::Active, start, end });
            break;
        case CombatActionTrackKind::Hit: {
            ACTION::CombatActionHitWindow value{};
            value.id = id;
            value.startTimeSeconds = start;
            value.endTimeSeconds = end;
            definition.hitWindows.push_back(std::move(value));
        } break;
        case CombatActionTrackKind::Cancel: {
            ACTION::CombatActionCancelWindow value{};
            value.id = id;
            value.startTimeSeconds = start;
            value.endTimeSeconds = end;
            definition.cancelWindows.push_back(std::move(value));
        } break;
        case CombatActionTrackKind::Motion: {
            ACTION::CombatActionMotionWindow value{};
            value.id = id;
            value.mode = ACTION::CombatMotionMode::MotionWarp;
            value.startTimeSeconds = start;
            value.endTimeSeconds = end;
            definition.motionWindows.push_back(std::move(value));
        } break;
        case CombatActionTrackKind::Defense: {
            ACTION::CombatActionDefenseWindow value{};
            value.id = id;
            value.startTimeSeconds = start;
            value.endTimeSeconds = end;
            definition.defenseWindows.push_back(std::move(value));
        } break;
        case CombatActionTrackKind::Vfx: {
            ACTION::CombatActionVfxCue value{};
            value.id = id;
            value.timeSeconds = start;
            definition.presentation.vfxCues.push_back(std::move(value));
        } break;
        default: return;
        }
        ACTION::NormalizeCombatAction(definition);
        selection_ = { track, id };
        RecordMutation(std::move(before));
        statusMessage_ = std::string(TrackName(track)) + " item added";
    }

    void CombatActionWorkspaceController::RecordMutation(
        ACTION::CombatActionDefinition before,
        uint64_t mergeGroup) {
        document_.RecordApplied(std::move(before), mergeGroup);
    }

} // namespace HIKARI::EDITOR
