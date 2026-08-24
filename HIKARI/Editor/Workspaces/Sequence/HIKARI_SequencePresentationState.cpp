#include "Editor/Workspaces/Sequence/HIKARI_SequencePresentationPanel.h"

#include <algorithm>
#include <cmath>

#include "Editor/Workspaces/Sequence/HIKARI_SequenceBindingPanel.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::EDITOR {
    bool SequencePresentationPanel::HasAnimator(
        const DocumentSceneBase& scene,
        SceneObjectId objectId) const {
        const GameObject* object = scene.GetWorld().FindObject(objectId);
        return object != nullptr &&
            object->GetComponent<AnimatorComponent>() != nullptr;
    }

    void SequencePresentationPanel::Select(
        SequencePresentationItemKind kind,
        uint64_t itemId) noexcept {
        selectedKind_ = kind;
        selectedItemId_ = itemId;
    }

    void SequencePresentationPanel::NormalizeSelection(
        const CinematicSequence& sequence) noexcept {
        bool valid = selectedKind_ == SequencePresentationItemKind::None ||
            selectedItemId_ == 0;
        switch (selectedKind_) {
        case SequencePresentationItemKind::ActorAnimation:
            valid = std::any_of(
                sequence.actorAnimationTrack.clips.begin(),
                sequence.actorAnimationTrack.clips.end(),
                [this](const auto& value) {
                    return value.id == selectedItemId_;
                });
            break;
        case SequencePresentationItemKind::Vfx:
            valid = std::any_of(
                sequence.vfxTrack.cues.begin(),
                sequence.vfxTrack.cues.end(),
                [this](const auto& value) {
                    return value.id == selectedItemId_;
                });
            break;
        case SequencePresentationItemKind::TimeDilation:
            valid = std::any_of(
                sequence.timeDilationTrack.clips.begin(),
                sequence.timeDilationTrack.clips.end(),
                [this](const auto& value) {
                    return value.id == selectedItemId_;
                });
            break;
        case SequencePresentationItemKind::Signal:
            valid = std::any_of(
                sequence.signalTrack.cues.begin(),
                sequence.signalTrack.cues.end(),
                [this](const auto& value) {
                    return value.id == selectedItemId_;
                });
            break;
        case SequencePresentationItemKind::None:
        default:
            break;
        }
        if (!valid) Select(SequencePresentationItemKind::None, 0);
    }

    bool SequencePresentationPanel::DeleteSelection(
        CinematicSequence& sequence) {
        const auto eraseId = [this](auto& values) {
            const auto before = values.size();
            std::erase_if(values, [this](const auto& value) {
                return value.id == selectedItemId_;
            });
            return values.size() != before;
        };
        bool changed = false;
        switch (selectedKind_) {
        case SequencePresentationItemKind::ActorAnimation:
            changed = eraseId(sequence.actorAnimationTrack.clips);
            break;
        case SequencePresentationItemKind::Vfx:
            changed = eraseId(sequence.vfxTrack.cues);
            break;
        case SequencePresentationItemKind::TimeDilation:
            changed = eraseId(sequence.timeDilationTrack.clips);
            break;
        case SequencePresentationItemKind::Signal:
            changed = eraseId(sequence.signalTrack.cues);
            break;
        case SequencePresentationItemKind::None:
        default:
            break;
        }
        if (changed) Select(SequencePresentationItemKind::None, 0);
        return changed;
    }

    float SequencePresentationPanel::SnapTime(float value) const noexcept {
        if (!snapEnabled_ || snapFramesPerSecond_ <= 0) return value;
        const float frames = value * static_cast<float>(snapFramesPerSecond_);
        return std::round(frames) /
            static_cast<float>(snapFramesPerSecond_);
    }

    SEQUENCER::SequenceBindingId
    SequencePresentationPanel::ResolveActorBinding(
        DocumentSceneBase& scene,
        CinematicSequence& sequence,
        SceneObjectId selectedObjectId,
        bool portableAsset,
        SEQUENCER::SequenceBindingContext& previewBindings) {
        if (!HasAnimator(scene, selectedObjectId)) return {};
        if (!portableAsset) {
            return SEQUENCER::FindOrCreateSceneObjectBinding(
                sequence.bindings, selectedObjectId,
                scene.GetWorld().FindObject(selectedObjectId)->GetName());
        }
        return FindOrCreatePortableActorBinding(
            scene.GetSceneDocument(), sequence,
            selectedObjectId, previewBindings);
    }

    SequencePresentationItemKind
    SequencePresentationPanel::GetSelectedKind() const noexcept {
        return selectedKind_;
    }

    uint64_t SequencePresentationPanel::GetSelectedItemId() const noexcept {
        return selectedItemId_;
    }

} // namespace HIKARI::EDITOR
