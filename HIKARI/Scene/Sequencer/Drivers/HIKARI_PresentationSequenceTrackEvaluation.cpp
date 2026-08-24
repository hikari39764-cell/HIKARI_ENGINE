#include "Scene/Sequencer/Drivers/HIKARI_PresentationSequenceTrackDriver.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Sequencer/Tracks/HIKARI_PresentationTracks.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::SEQUENCER {
    namespace {
        struct ActiveAnimation {
            SceneObjectId objectId{};
            const ActorAnimationClip* clip = nullptr;
        };

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

        bool BuildWorldTransform(
            const GameObject* object,
            const SequenceVfxCue& cue,
            VFX::VfxWorldTransform& outTransform) noexcept {
            constexpr float degreesToRadians =
                std::numbers::pi_v<float> / 180.0f;
            const MATH::Quat cueRotation = MATH::Quat::FromEulerXYZ(
                cue.rotationEulerDeg.x * degreesToRadians,
                cue.rotationEulerDeg.y * degreesToRadians,
                cue.rotationEulerDeg.z * degreesToRadians);
            MATH::Mat4 world = MATH::Mat4::TRS(
                cue.positionOffset,
                cueRotation,
                { cue.scale, cue.scale, cue.scale });
            if (object != nullptr) {
                world = object->GetTransform().GetWorldMatrix() * world;
            }
            return MATH::DecomposeTRS(
                world,
                outTransform.position,
                outTransform.rotation,
                outTransform.scale);
        }

        uint64_t MakeTimeSourceId(
            SequencePlaybackHandle handle,
            uint64_t clipId) noexcept {
            uint64_t value = handle.value ^
                (handle.epoch + 0x9e3779b97f4a7c15ull +
                    (handle.value << 6u) + (handle.value >> 2u));
            value ^= clipId + 0x9e3779b97f4a7c15ull +
                (value << 6u) + (value >> 2u);
            return value == 0 ? 1 : value;
        }
    }

    void PresentationSequenceTrackDriver::EvaluateAnimation(
        InstanceState& state,
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        const ActorAnimationTrack& track =
            instance.asset->sequence.actorAnimationTrack;
        std::vector<ActiveAnimation> active{};
        if (track.enabled) {
            for (const ActorAnimationClip& clip : track.clips) {
                if (context.currentTimeSeconds < clip.startTimeSeconds ||
                    context.currentTimeSeconds >=
                        clip.startTimeSeconds + clip.durationSeconds) {
                    continue;
                }
                SceneObjectId objectId{};
                if (!ResolveBinding(
                        instance,
                        clip.actorBindingId,
                        objectId)) {
                    continue;
                }
                const auto found = std::find_if(
                    active.begin(),
                    active.end(),
                    [objectId](const ActiveAnimation& candidate) {
                        return candidate.objectId == objectId;
                    });
                if (found != active.end()) {
                    found->clip = &clip;
                } else {
                    active.push_back({ objectId, &clip });
                }
            }
        }

        std::vector<SceneObjectId> activeObjects{};
        activeObjects.reserve(active.size());
        const float evaluationDelta = std::abs(
            context.currentTimeSeconds - context.previousTimeSeconds);
        for (const ActiveAnimation& entry : active) {
            GameObject* object = world_.FindObject(entry.objectId);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>()
                : nullptr;
            if (animator == nullptr || entry.clip == nullptr ||
                entry.clip->clipName.empty()) {
                continue;
            }
            activeObjects.push_back(entry.objectId);
            AnimatorSnapshot& snapshot = CaptureAnimator(
                state,
                entry.objectId,
                *animator);
            const bool clipChanged = snapshot.activeClipId != entry.clip->id;
            if (clipChanged) {
                animator->CrossFade(
                    entry.clip->clipName,
                    entry.clip->blendInSeconds,
                    entry.clip->loop,
                    true);
                snapshot.activeClipId = entry.clip->id;
            }
            const float local = context.currentTimeSeconds -
                entry.clip->startTimeSeconds;
            float sourceTime = entry.clip->sourceStartTimeSeconds +
                local * entry.clip->playbackRate;
            if (!context.IsDiscontinuous() && context.playing) {
                sourceTime -= evaluationDelta * entry.clip->playbackRate;
            }
            animator->SetLoop(entry.clip->loop);
            animator->SetSpeed(entry.clip->playbackRate);
            animator->SetTime((std::max)(0.0f, sourceTime));
            animator->SetPlaying(
                context.playing && !context.IsDiscontinuous());
        }
        RestoreInactiveAnimators(state, activeObjects);
    }

    void PresentationSequenceTrackDriver::EvaluateVfx(
        InstanceState& state,
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        const SequenceVfxTrack& track = instance.asset->sequence.vfxTrack;
        if (!track.enabled) return;
        std::erase_if(
            state.vfxHandles,
            [this](VFX::VfxPlaybackHandle handle) {
                return !vfxPlayback_.IsActive(handle);
            });
        for (const SequenceVfxCue& cue : track.cues) {
            if (!cue.assetGuid.IsValid() ||
                !DidSequenceTimeCrossCue(
                    context,
                    cue.timeSeconds,
                    cue.fireOnSeek)) {
                continue;
            }
            SceneObjectId ownerObjectId{};
            const bool hasOwner = ResolveBinding(
                instance,
                cue.ownerBindingId,
                ownerObjectId);
            GameObject* owner = hasOwner
                ? world_.FindObject(ownerObjectId)
                : nullptr;
            VFX::VfxPlayRequest request{};
            request.assetGuid = cue.assetGuid;
            request.randomSeed = cue.randomSeed;
            request.ownerObject = owner != nullptr
                ? owner->GetRuntimeHandle()
                : RuntimeObjectHandle{};
            if (!BuildWorldTransform(owner, cue, request.transform)) {
                continue;
            }
            const VFX::VfxPlayResult play =
                vfxPlayback_.PlayDetailed(request);
            if (play.IsAccepted()) {
                state.vfxHandles.push_back(play.handle);
            }
        }
    }

    void PresentationSequenceTrackDriver::EvaluateTimeDilation(
        InstanceState& state,
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        const SequenceTimeDilationClip* active =
            EvaluateSequenceTimeDilationTrack(
                instance.asset->sequence.timeDilationTrack,
                context.currentTimeSeconds);
        if (active == nullptr) {
            if (state.timeDilationToken != 0) {
                (void)timeDilation_.Release(
                    &world_,
                    state.timeDilationToken);
            }
            state.timeDilationToken = 0;
            state.timeDilationClipId = 0;
            return;
        }
        if (state.timeDilationToken != 0 &&
            state.timeDilationClipId == active->id &&
            state.timeDilationScale == active->scale &&
            state.timeDilationPriority == active->priority) {
            return;
        }
        if (state.timeDilationToken != 0) {
            (void)timeDilation_.Release(
                &world_,
                state.timeDilationToken);
        }
        state.timeDilationToken = timeDilation_.Request(
            &world_,
            MakeTimeSourceId(state.handle, active->id),
            active->priority,
            active->scale,
            0.0f);
        state.timeDilationClipId = active->id;
        state.timeDilationScale = active->scale;
        state.timeDilationPriority = active->priority;
    }

    void PresentationSequenceTrackDriver::EvaluateSignals(
        const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {

        const SequenceSignalTrack& track =
            instance.asset->sequence.signalTrack;
        if (!track.enabled) return;
        for (const SequenceSignalCue& cue : track.cues) {
            if (cue.name.empty() || !DidSequenceTimeCrossCue(
                    context,
                    cue.timeSeconds,
                    cue.fireOnSeek)) {
                continue;
            }
            SceneObjectId target{};
            (void)ResolveBinding(
                instance,
                cue.targetBindingId,
                target);
            world_.FrameEvents().Publish(SequenceSignalEvent{
                cue.id,
                instance.asset->guid,
                target,
                cue.name,
                cue.payload
            });
        }
    }

} // namespace HIKARI::SEQUENCER
