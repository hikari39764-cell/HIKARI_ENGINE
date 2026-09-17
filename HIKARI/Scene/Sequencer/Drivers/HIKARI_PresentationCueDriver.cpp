#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "HIKARI_PresentationCueDriver.h"
#include <algorithm>
#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::SEQUENCER {
    PresentationCueDriver::PresentationCueDriver(World& world, VFX::VfxPlaybackService& vfx)
        : world_(world), vfx_(vfx) {}

    bool PresentationCueDriver::ResolveTransform(const SequencePlaybackInstanceView& instance,
        SequenceBindingId binding, VFX::VfxWorldTransform& out) const {
        out = {};
        if (!binding.IsValid()) return true;
        SceneObjectId objectId;
        if (!instance.asset || !instance.bindings ||
            !instance.bindings->Resolve(instance.asset->sequence.bindings, binding, objectId)) return false;
        const auto* object = world_.FindObject(objectId);
        return object && MATH::DecomposeTRS(object->GetTransform().GetWorldMatrix(),
            out.position, out.rotation, out.scale);
    }
    void PresentationCueDriver::Validate(const SequencePlaybackInstanceView& instance,
        std::vector<SequencePlaybackDiagnostic>& diagnostics) const {
        if (!instance.asset) return;
        std::string error;
        if (!ValidatePresentationCueTrack(instance.asset->sequence.presentationCueTrack, error))
            diagnostics.push_back({SequenceDiagnosticSeverity::Error, "InvalidCueTrack", "PresentationCue", error});
        for (const auto& clip : instance.asset->sequence.presentationCueTrack.clips) {
            VFX::VfxWorldTransform transform;
            if (!ResolveTransform(instance, clip.targetBinding, transform))
                diagnostics.push_back({SequenceDiagnosticSeverity::Error, "CueBindingMissing", "PresentationCue",
                    "Presentation cue target is unavailable: " + clip.name, clip.targetBinding});
        }
    }
    void PresentationCueDriver::OnSequenceStarted(const SequencePlaybackInstanceView& instance) {
        if (!instance.asset) return;
        InstanceState state;
        state.handle = instance.handle;
        const auto& track = instance.asset->sequence.presentationCueTrack;
        state.cues.resize(track.clips.size());
        if (track.enabled) {
            for (size_t i = 0; i < track.clips.size(); ++i)
                if (track.clips[i].kind == PresentationCueKind::Audio) {
                    const auto path = PROJECT_PATHS::ResolveRuntimePayloadFile(GetProjectContext().GetRoot(), track.clips[i].soundPath);
                    if (!path.empty()) state.cues[i].sound = AUDIO::Load(TEXT::PathToUtf8(path).c_str());
                }
        }
        states_.push_back(std::move(state));
    }
    void PresentationCueDriver::StopCue(CueState& cue) {
        if (cue.voice >= 0) AUDIO::Stop(cue.voice);
        if (cue.effect.IsValid()) vfx_.Stop(cue.effect);
        cue.voice = -1;
        cue.effect = {};
        cue.entered = false;
    }
    void PresentationCueDriver::Evaluate(const SequencePlaybackInstanceView& instance,
        const SequenceEvaluationContext& context) {
        const auto found = std::find_if(states_.begin(), states_.end(),
            [&](const auto& state) { return state.handle == instance.handle; });
        if (found == states_.end() || !instance.asset) return;
        const auto& track = instance.asset->sequence.presentationCueTrack;
        for (size_t i = 0; i < found->cues.size() && i < track.clips.size(); ++i) {
            auto& state = found->cues[i];
            const auto& clip = track.clips[i];
            const float localTime = context.currentTimeSeconds - clip.startSeconds;
            // Reverse camera playback remains supported; sound/VFX are muted
            // in reverse, rather than pretending to simulate particles backwards.
            if (context.IsPreview() || !track.enabled || context.playbackRate <= 0.0f ||
                localTime < 0.0f || localTime >= clip.durationSeconds) {
                StopCue(state); continue;
            }
            if (context.IsDiscontinuous()) StopCue(state);
            VFX::VfxWorldTransform transform;
            if (!ResolveTransform(instance, clip.targetBinding, transform)) {
                StopCue(state); continue;
            }
            if (!state.entered) {
                state.entered = true;
                if (clip.kind == PresentationCueKind::Audio) {
                    auto options = clip.audio;
                    options.startSeconds = localTime;
                    options.playbackRate = context.playbackRate;
                    options.paused = context.playing ? 0u : 1u;
                    options.source.position = {transform.position.x, transform.position.y, transform.position.z};
                    state.voice = AUDIO::PlayCue(state.sound, options, world_.GetRuntimeIdentity());
                    if (state.voice < 0 && !state.warned) {
                        HIKARI_LOG_WARN("[Sequence][AudioCue] unavailable, expired, or budget rejected: " + clip.name);
                        state.warned = true;
                    }
                } else {
                    VFX::VfxPlayRequest request;
                    request.assetGuid = clip.vfxAsset;
                    request.transform = transform;
                    request.randomSeed = static_cast<uint32_t>(clip.id);
                    state.effect = vfx_.PlayDetailed(request).handle;
                    if (localTime > 1.0e-4f && state.effect.IsValid())
                        (void)vfx_.QueueSeek(state.effect, localTime);
                    if (!state.effect.IsValid() && !state.warned) {
                        HIKARI_LOG_WARN("[Sequence][VfxCue] playback rejected: " + clip.name);
                        state.warned = true;
                    }
                }
            }
            if (clip.kind == PresentationCueKind::Audio && state.voice >= 0) {
                if (clip.audio.spatial) {
                    auto source = clip.audio.source;
                    source.position = {transform.position.x, transform.position.y, transform.position.z};
                    AUDIO::SetSource(state.voice, source);
                }
                AUDIO::SetPlaybackRate(state.voice, context.playbackRate);
                if (context.playing) AUDIO::Resume(state.voice); else AUDIO::Pause(state.voice);
            } else if (state.effect.IsValid()) {
                vfx_.SetTransform(state.effect, transform);
                vfx_.SetTimeScale(state.effect, context.playing ? context.playbackRate : 0.0f);
            }
        }
    }
    void PresentationCueDriver::OnSequenceStopped(SequencePlaybackHandle handle, SequenceStopReason) {
        for (auto& state : states_)
            if (state.handle == handle) for (auto& cue : state.cues) StopCue(cue);
        std::erase_if(states_, [&](const auto& state) { return state.handle == handle; });
    }
    void PresentationCueDriver::Reset() {
        for (auto& state : states_) for (auto& cue : state.cues) StopCue(cue);
        states_.clear();
    }
}
