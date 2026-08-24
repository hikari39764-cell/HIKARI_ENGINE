#include "Editor/Workspaces/Sequence/HIKARI_SequencePresentationPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {

template <class T>
T* FindById(std::vector<T>& values, uint64_t id) noexcept {
    const auto found = std::find_if(
        values.begin(), values.end(),
        [id](const T& value) { return value.id == id; });
    return found != values.end() ? &*found : nullptr;
}

#if defined(HIKARI_WITH_EDITOR)
bool InputText(std::string_view label, std::string& value,
               size_t capacity = 256) {
    std::vector<char> buffer((std::max)(capacity, value.size() + 32), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (!ImGui::InputText(std::string(label).c_str(), buffer.data(),
                          buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}

bool DrawBindingCombo(
    const char* label,
    const SEQUENCER::SequenceBindingTable& bindings,
    SEQUENCER::SequenceBindingId& bindingId,
    bool allowNone = true) {
    const SEQUENCER::SequenceBinding* current =
        SEQUENCER::FindSequenceBinding(bindings, bindingId);
    const char* preview = current != nullptr ? current->name.c_str() : "None";
    if (!ImGui::BeginCombo(label, preview)) return false;
    bool changed = false;
    if (allowNone && ImGui::Selectable("None", !bindingId.IsValid())) {
        bindingId = {};
        changed = true;
    }
    for (const auto& binding : bindings) {
        const bool selected = binding.id == bindingId;
        std::string row = binding.name;
        if (binding.targetKind == SEQUENCER::SequenceBindingTargetKind::Slot) {
            row += "  [Slot]";
        }
        if (ImGui::Selectable(row.c_str(), selected)) {
            bindingId = binding.id;
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

const char* SelectionTitle(SequencePresentationItemKind kind) noexcept {
    switch (kind) {
    case SequencePresentationItemKind::ActorAnimation:
        return "ACTOR ANIMATION";
    case SequencePresentationItemKind::Vfx:
        return "VFX CUE";
    case SequencePresentationItemKind::TimeDilation:
        return "TIME DILATION";
    case SequencePresentationItemKind::Signal:
        return "SEQUENCE EVENT";
    case SequencePresentationItemKind::None:
    default:
        return "SEQUENCE OVERVIEW";
    }
}
#endif

bool ContainsObject(
    const std::vector<SceneObjectId>& objects,
    SceneObjectId objectId) noexcept {
    return std::find(objects.begin(), objects.end(), objectId) != objects.end();
}

} // namespace

SequencePresentationPanelResult SequencePresentationPanel::DrawInspector(
    AssetDatabase& assetDatabase,
    CinematicSequence& sequence,
    bool editingAllowed) {
    SequencePresentationPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
    NormalizeSelection(sequence);
    ImGui::TextDisabled("%s", SelectionTitle(selectedKind_));
    ImGui::SameLine();
    ImGui::TextDisabled("  id %llu", selectedItemId_);
    ImGui::Separator();
    if (!editingAllowed) ImGui::BeginDisabled();

    if (selectedKind_ == SequencePresentationItemKind::None) {
        if (InputText("Sequence Name", sequence.name, 160)) {
            result.sequenceChanged = true;
            result.editMergeId = 0x53530001ull;
        }
        if (ImGui::DragFloat("Duration", &sequence.durationSeconds, 0.01f,
                             kMinCinematicSequenceDurationSeconds,
                             kMaxCinematicSequenceDurationSeconds, "%.3f s")) {
            result.sequenceChanged = true;
            result.editMergeId = 0x53530002ull;
        }
        ImGui::Spacing();
        ImGui::SeparatorText("PRODUCTION SUMMARY");
        ImGui::Text("%zu actor clips",
                    sequence.actorAnimationTrack.clips.size());
        ImGui::Text("%zu VFX cues", sequence.vfxTrack.cues.size());
        ImGui::Text("%zu slow-motion clips",
                    sequence.timeDilationTrack.clips.size());
        ImGui::Text("%zu sequence events", sequence.signalTrack.cues.size());
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Camera tracks remain in the Camera Timeline. Stage tracks author "
            "actors, effects, time accents, and gameplay-facing events against "
            "the same playhead.");
    } else if (selectedKind_ ==
                   SequencePresentationItemKind::ActorAnimation) {
        auto* clip = FindById(
            sequence.actorAnimationTrack.clips, selectedItemId_);
        if (clip != nullptr) {
            if (DrawBindingCombo("Actor", sequence.bindings,
                                 clip->actorBindingId, false)) {
                result.sequenceChanged = true;
            }
            if (InputText("Animation Clip", clip->clipName, 192)) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Start", &clip->startTimeSeconds, 0.005f,
                                 0.0f, sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Duration", &clip->durationSeconds, 0.005f,
                    SEQUENCER::kMinSequenceClipDurationSeconds,
                    sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Source Start",
                                 &clip->sourceStartTimeSeconds, 0.005f,
                                 0.0f, 3600.0f, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::SliderFloat("Playback Rate", &clip->playbackRate,
                                   0.05f,
                                   SEQUENCER::kMaxActorAnimationPlaybackRate,
                                   "%.2fx")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Blend In", &clip->blendInSeconds, 0.005f,
                                 0.0f, clip->durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::Checkbox("Loop Source", &clip->loop)) {
                result.sequenceChanged = true;
            }
            ImGui::TextDisabled(
                "The bound actor is posed live in the scene while scrubbing.");
        }
    } else if (selectedKind_ == SequencePresentationItemKind::Vfx) {
        auto* cue = FindById(sequence.vfxTrack.cues, selectedItemId_);
        if (cue != nullptr) {
            std::string assetGuid = cue->assetGuid.value;
            if (DrawAssetField(
                    &assetDatabase,
                    {"Effect Asset", AssetType::VfxEffect}, assetGuid)) {
                cue->assetGuid.value = std::move(assetGuid);
                result.sequenceChanged = true;
            }
            if (DrawBindingCombo("Attach To", sequence.bindings,
                                 cue->ownerBindingId, true)) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Time", &cue->timeSeconds, 0.005f, 0.0f,
                                 sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat3("Position Offset", &cue->positionOffset.x,
                                  0.01f)) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat3("Rotation", &cue->rotationEulerDeg.x,
                                  0.25f, -360.0f, 360.0f, "%.1f deg")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Scale", &cue->scale, 0.01f, 0.001f,
                                 100.0f, "%.2fx")) {
                result.sequenceChanged = true;
            }
            int seed = static_cast<int>(cue->randomSeed);
            if (ImGui::InputInt("Random Seed", &seed)) {
                cue->randomSeed = static_cast<uint32_t>((std::max)(0, seed));
                result.sequenceChanged = true;
            }
            if (ImGui::Checkbox("Preview When Seeking", &cue->fireOnSeek)) {
                result.sequenceChanged = true;
            }
            ImGui::TextDisabled(
                "The runtime owns spawned-effect lifetime and stops it with "
                "the sequence.");
        }
    } else if (selectedKind_ ==
                   SequencePresentationItemKind::TimeDilation) {
        auto* clip = FindById(
            sequence.timeDilationTrack.clips, selectedItemId_);
        if (clip != nullptr) {
            if (ImGui::DragFloat("Start", &clip->startTimeSeconds, 0.005f,
                                 0.0f, sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Duration", &clip->durationSeconds, 0.005f,
                    SEQUENCER::kMinSequenceClipDurationSeconds,
                    sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::SliderFloat("Game Time Scale", &clip->scale,
                                   SEQUENCER::kMinSequenceTimeScale, 1.0f,
                                   "%.2fx")) {
                result.sequenceChanged = true;
            }
            if (ImGui::SliderInt("Priority", &clip->priority, 0, 1000)) {
                result.sequenceChanged = true;
            }
            ImGui::TextDisabled(
                "Uses the shared time-dilation arbiter; it never writes the "
                "global clock directly.");
        }
    } else if (selectedKind_ == SequencePresentationItemKind::Signal) {
        auto* cue = FindById(sequence.signalTrack.cues, selectedItemId_);
        if (cue != nullptr) {
            if (InputText("Event Name", cue->name, 192)) {
                result.sequenceChanged = true;
            }
            if (InputText("Payload", cue->payload, 512)) {
                result.sequenceChanged = true;
            }
            if (DrawBindingCombo("Target", sequence.bindings,
                                 cue->targetBindingId, true)) {
                result.sequenceChanged = true;
            }
            if (ImGui::DragFloat("Time", &cue->timeSeconds, 0.005f, 0.0f,
                                 sequence.durationSeconds, "%.3f s")) {
                result.sequenceChanged = true;
            }
            if (ImGui::Checkbox("Fire When Seeking", &cue->fireOnSeek)) {
                result.sequenceChanged = true;
            }
            ImGui::TextDisabled(
                "Events are published as typed frame events for gameplay or "
                "presentation systems.");
        }
    }

    if (selectedKind_ != SequencePresentationItemKind::None) {
        ImGui::Separator();
        if (ImGui::Button("Delete Selected Item", ImVec2(-1.0f, 0.0f)) &&
            DeleteSelection(sequence)) {
            result.sequenceChanged = true;
            result.selectionChanged = true;
            result.editMergeId = 0;
        }
    }
    if (!editingAllowed) ImGui::EndDisabled();

    if (result.sequenceChanged) {
        NormalizeCinematicSequence(sequence);
        NormalizeSelection(sequence);
        if (result.editMergeId == 0) {
            result.editMergeId = 0x53540000ull ^ selectedItemId_;
        }
    }
#else
    (void)assetDatabase;
    (void)sequence;
    (void)editingAllowed;
#endif
    return result;
}

SequencePresentationPanel::PreviewAnimatorSnapshot&
SequencePresentationPanel::CaptureAnimator(
    World& world, SceneObjectId objectId) {
    const auto found = std::find_if(
        previewAnimators_.begin(), previewAnimators_.end(),
        [objectId](const PreviewAnimatorSnapshot& snapshot) {
            return snapshot.objectId == objectId;
        });
    if (found != previewAnimators_.end()) return *found;

    PreviewAnimatorSnapshot snapshot{};
    snapshot.objectId = objectId;
    GameObject* object = world.FindObject(objectId);
    AnimatorComponent* animator = object != nullptr
        ? object->GetComponent<AnimatorComponent>() : nullptr;
    if (animator != nullptr) {
        snapshot.motion = animator->GetMotionSample();
        snapshot.timeSeconds = animator->GetTime();
        snapshot.speed = animator->GetSpeed();
        snapshot.loop = animator->GetLoop();
        snapshot.autoPlay = animator->GetAutoPlay();
        snapshot.rootMotionEnabled = animator->IsRootMotionEnabled();
        snapshot.playing = animator->IsPlaying();
    }
    previewAnimators_.push_back(std::move(snapshot));
    return previewAnimators_.back();
}

void SequencePresentationPanel::ApplyActorPreview(
    World& world,
    const CinematicSequence& sequence,
    const SEQUENCER::SequenceBindingContext& bindings,
    float timeSeconds) {
    std::vector<SceneObjectId> activeObjects{};
    if (sequence.actorAnimationTrack.enabled) {
        for (const auto& clip : sequence.actorAnimationTrack.clips) {
            if (timeSeconds < clip.startTimeSeconds ||
                timeSeconds >= clip.startTimeSeconds + clip.durationSeconds ||
                clip.clipName.empty()) {
                continue;
            }
            SceneObjectId objectId{};
            if (!SEQUENCER::ResolveSceneObjectBinding(
                    sequence.bindings, clip.actorBindingId, bindings,
                    objectId)) {
                continue;
            }
            GameObject* object = world.FindObject(objectId);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>() : nullptr;
            if (animator == nullptr) continue;
            (void)CaptureAnimator(world, objectId);
            if (!ContainsObject(activeObjects, objectId)) {
                activeObjects.push_back(objectId);
            }
            if (animator->GetClip() != clip.clipName) {
                animator->SetClip(clip.clipName);
            }
            const float sourceTime = clip.sourceStartTimeSeconds +
                (timeSeconds - clip.startTimeSeconds) * clip.playbackRate;
            animator->SetLoop(clip.loop);
            animator->SetSpeed(clip.playbackRate);
            animator->SetTime((std::max)(0.0f, sourceTime));
            animator->SetPlaying(false);
        }
    }
    RestoreInactivePreviewActors(world, activeObjects);
}

void SequencePresentationPanel::RestoreInactivePreviewActors(
    World& world, const std::vector<SceneObjectId>& activeObjects) noexcept {
    for (size_t index = previewAnimators_.size(); index-- > 0;) {
        PreviewAnimatorSnapshot& snapshot = previewAnimators_[index];
        if (ContainsObject(activeObjects, snapshot.objectId)) continue;
        GameObject* object = world.FindObject(snapshot.objectId);
        AnimatorComponent* animator = object != nullptr
            ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (animator != nullptr) {
            animator->PlayMotion(snapshot.motion, snapshot.speed,
                                 snapshot.loop, true);
            animator->SetTime(snapshot.timeSeconds);
            animator->SetAutoPlay(snapshot.autoPlay);
            animator->SetRootMotionEnabled(snapshot.rootMotionEnabled);
            animator->SetPlaying(snapshot.playing);
        }
        previewAnimators_.erase(previewAnimators_.begin() + index);
    }
}

void SequencePresentationPanel::RestoreActorPreview(World& world) noexcept {
    RestoreInactivePreviewActors(world, {});
}

void SequencePresentationPanel::Reset(World* world) noexcept {
    if (world != nullptr) RestoreActorPreview(*world);
    previewAnimators_.clear();
    Select(SequencePresentationItemKind::None, 0);
    selectedBindingId_ = {};
    dragMode_ = DragMode::None;
    panning_ = false;
    scrubbing_ = false;
    scrollTimeSeconds_ = 0.0f;
}

} // namespace HIKARI::EDITOR
