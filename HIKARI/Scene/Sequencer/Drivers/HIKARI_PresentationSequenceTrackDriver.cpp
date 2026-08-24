#include "Scene/Sequencer/Drivers/HIKARI_PresentationSequenceTrackDriver.h"

#include <algorithm>
#include <unordered_set>

#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Tracks/HIKARI_PresentationTracks.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::SEQUENCER {

    namespace {
        bool ContainsObject(
            const std::vector<SceneObjectId>& objects,
            SceneObjectId objectId) noexcept {

            return std::any_of(
                objects.begin(),
                objects.end(),
                [objectId](SceneObjectId candidate) {
                    return candidate == objectId;
                });
        }

        bool ResolveBinding(
            const SequencePlaybackInstanceView& instance,
            SequenceBindingId bindingId,
            SceneObjectId& outObjectId) noexcept {

            return bindingId.IsValid() && instance.asset != nullptr &&
                instance.bindings != nullptr &&
                instance.bindings->Resolve(
                    instance.asset->sequence.bindings,
                    bindingId,
                    outObjectId);
        }

    }

    PresentationSequenceTrackDriver::PresentationSequenceTrackDriver(
        World& world,
        VFX::VfxPlaybackService& vfxPlayback,
        SERVICES::ActionTimeDilationService& timeDilation) noexcept
        : world_(world)
        , vfxPlayback_(vfxPlayback)
        , timeDilation_(timeDilation) {
    }

    std::string_view
        PresentationSequenceTrackDriver::GetDriverId() const noexcept {
        return "Presentation";
    }

    void PresentationSequenceTrackDriver::OnSequenceStarted(
        const SequencePlaybackInstanceView& instance) {
        (void)EnsureState(instance.handle);
    }

    void PresentationSequenceTrackDriver::Validate(
        const SequencePlaybackInstanceView& instance,
        std::vector<SequencePlaybackDiagnostic>& diagnostics) const {

        if (instance.asset == nullptr || instance.bindings == nullptr) {
            return;
        }
        const CinematicSequence& sequence = instance.asset->sequence;
        std::unordered_set<uint64_t> actorBindings{};
        for (const ActorAnimationClip& clip :
                sequence.actorAnimationTrack.clips) {
            if (!clip.actorBindingId.IsValid() ||
                !actorBindings.insert(clip.actorBindingId.value).second) {
                continue;
            }
            SceneObjectId objectId{};
            if (!ResolveBinding(instance, clip.actorBindingId, objectId)) {
                continue;
            }
            const GameObject* object = world_.FindObject(objectId);
            if (object != nullptr &&
                object->GetComponent<AnimatorComponent>() != nullptr) {
                continue;
            }
            diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                object == nullptr
                    ? "ActorBindingObjectMissing"
                    : "ActorAnimatorMissing",
                "Presentation",
                object == nullptr
                    ? "Animation binding resolves outside the active scene"
                    : "Animation binding target has no AnimatorComponent",
                clip.actorBindingId,
                {}
            });
        }
        for (const SequenceVfxCue& cue : sequence.vfxTrack.cues) {
            if (!cue.assetGuid.IsValid()) {
                diagnostics.push_back({
                    SequenceDiagnosticSeverity::Error,
                    "VfxAssetMissing",
                    "Presentation",
                    "VFX cue has no asset",
                    cue.ownerBindingId,
                    {}
                });
            }
        }
        for (const SequenceSignalCue& cue : sequence.signalTrack.cues) {
            if (cue.name.empty()) {
                diagnostics.push_back({
                    SequenceDiagnosticSeverity::Warning,
                    "SignalNameMissing",
                    "Presentation",
                    "Signal cue has no event name",
                    cue.targetBindingId,
                    {}
                });
            }
        }
    }

    void PresentationSequenceTrackDriver::Evaluate(
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        if (instance.asset == nullptr || instance.bindings == nullptr) {
            return;
        }
        InstanceState& state = EnsureState(instance.handle);
        EvaluateAnimation(state, instance, context);
        EvaluateVfx(state, instance, context);
        EvaluateTimeDilation(state, instance, context);
        EvaluateSignals(instance, context);
    }

    void PresentationSequenceTrackDriver::OnSequenceStopped(
        SequencePlaybackHandle handle,
        SequenceStopReason) {

        const auto found = std::find_if(
            states_.begin(),
            states_.end(),
            [handle](const InstanceState& state) {
                return state.handle == handle;
            });
        if (found == states_.end()) return;
        ReleaseState(*found);
        states_.erase(found);
    }

    void PresentationSequenceTrackDriver::Reset() {
        for (InstanceState& state : states_) {
            ReleaseState(state);
        }
        states_.clear();
    }

    PresentationSequenceTrackDriver::InstanceState&
        PresentationSequenceTrackDriver::EnsureState(
            SequencePlaybackHandle handle) {

        if (InstanceState* state = FindState(handle)) {
            return *state;
        }
        states_.push_back({ handle });
        return states_.back();
    }

    PresentationSequenceTrackDriver::InstanceState*
        PresentationSequenceTrackDriver::FindState(
            SequencePlaybackHandle handle) noexcept {

        const auto found = std::find_if(
            states_.begin(),
            states_.end(),
            [handle](const InstanceState& state) {
                return state.handle == handle;
            });
        return found != states_.end() ? &*found : nullptr;
    }

    PresentationSequenceTrackDriver::AnimatorSnapshot&
        PresentationSequenceTrackDriver::CaptureAnimator(
            InstanceState& state,
            SceneObjectId objectId,
            AnimatorComponent& animator) {

        const auto found = std::find_if(
            state.animators.begin(),
            state.animators.end(),
            [objectId](const AnimatorSnapshot& snapshot) {
                return snapshot.objectId == objectId;
            });
        if (found != state.animators.end()) return *found;

        AnimatorSnapshot snapshot{};
        snapshot.objectId = objectId;
        snapshot.motion = animator.GetMotionSample();
        snapshot.timeSeconds = animator.GetTime();
        snapshot.speed = animator.GetSpeed();
        snapshot.loop = animator.GetLoop();
        snapshot.autoPlay = animator.GetAutoPlay();
        snapshot.rootMotionEnabled = animator.IsRootMotionEnabled();
        snapshot.playing = animator.IsPlaying();
        state.animators.push_back(std::move(snapshot));
        return state.animators.back();
    }

    void PresentationSequenceTrackDriver::RestoreAnimator(
        AnimatorSnapshot& snapshot) noexcept {

        GameObject* object = world_.FindObject(snapshot.objectId);
        AnimatorComponent* animator = object != nullptr
            ? object->GetComponent<AnimatorComponent>()
            : nullptr;
        if (animator == nullptr) return;
        animator->PlayMotion(
            snapshot.motion,
            snapshot.speed,
            snapshot.loop,
            true);
        animator->SetTime(snapshot.timeSeconds);
        animator->SetAutoPlay(snapshot.autoPlay);
        animator->SetRootMotionEnabled(snapshot.rootMotionEnabled);
        animator->SetPlaying(snapshot.playing);
    }

    void PresentationSequenceTrackDriver::RestoreInactiveAnimators(
        InstanceState& state,
        const std::vector<SceneObjectId>& activeObjects) {

        for (size_t index = state.animators.size(); index-- > 0;) {
            if (ContainsObject(
                    activeObjects,
                    state.animators[index].objectId)) {
                continue;
            }
            RestoreAnimator(state.animators[index]);
            state.animators.erase(state.animators.begin() + index);
        }
    }

    void PresentationSequenceTrackDriver::ReleaseState(
        InstanceState& state) noexcept {

        for (AnimatorSnapshot& snapshot : state.animators) {
            RestoreAnimator(snapshot);
        }
        state.animators.clear();
        for (VFX::VfxPlaybackHandle handle : state.vfxHandles) {
            if (vfxPlayback_.IsActive(handle)) {
                (void)vfxPlayback_.Stop(handle);
            }
        }
        state.vfxHandles.clear();
        if (state.timeDilationToken != 0) {
            (void)timeDilation_.Release(&world_, state.timeDilationToken);
        }
        state.timeDilationToken = 0;
        state.timeDilationClipId = 0;
    }

} // namespace HIKARI::SEQUENCER
