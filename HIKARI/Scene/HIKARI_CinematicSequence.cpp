#include "Scene/HIKARI_CinematicSequence.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_set>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI {

namespace {} // namespace

void NormalizeCinematicSequence(CinematicSequence& sequence) {
    SEQUENCER::NormalizeSequenceBindings(sequence.bindings);
    SEQUENCER::NormalizeCameraCutTrack(sequence.cameraCutTrack);
    SEQUENCER::NormalizeScreenEffects(sequence.screenEffects);
    sequence.durationSeconds =
        std::clamp((std::max)(MATH::FiniteOr(sequence.durationSeconds, kMinCinematicSequenceDurationSeconds),
                              GetCinematicSequenceContentEnd(sequence)),
                   kMinCinematicSequenceDurationSeconds, kMaxCinematicSequenceDurationSeconds);
}

void NormalizeSceneCinematicsSettings(SceneCinematicsSettings& settings) {
    if (settings.sequences.empty()) {
        settings.sequences.push_back(CinematicSequence{});
    }

    std::unordered_set<uint64_t> usedIds{};
    CinematicSequenceId nextId = AllocateCinematicSequenceId(settings);
    for (CinematicSequence& sequence : settings.sequences) {
        if (!sequence.id.IsValid() || !usedIds.insert(sequence.id.value).second) {
            while (!usedIds.insert(nextId.value).second) {
                NUMERIC::AdvanceNonZero(nextId.value);
            }
            sequence.id = nextId;
            NUMERIC::AdvanceNonZero(nextId.value);
        }
        if (sequence.name.empty()) {
            sequence.name = "Sequence " + std::to_string(sequence.id.value);
        }
        NormalizeCinematicSequence(sequence);
    }

    if (FindCinematicSequence(settings, settings.defaultSequenceId) == nullptr) {
        settings.defaultSequenceId = settings.sequences.front().id;
    }
}

CinematicSequenceId AllocateCinematicSequenceId(const SceneCinematicsSettings& settings) {
    CinematicSequenceId nextId{1};
    for (const CinematicSequence& sequence : settings.sequences) {
        if (sequence.id.value < nextId.value) {
            continue;
        }
        if (sequence.id.value == (std::numeric_limits<uint64_t>::max)()) {
            nextId.value = 1;
            break;
        }
        nextId.value = sequence.id.value + 1;
    }
    while (FindCinematicSequence(settings, nextId) != nullptr) {
        NUMERIC::AdvanceNonZero(nextId.value);
    }
    return nextId;
}

CinematicSequence* FindCinematicSequence(SceneCinematicsSettings& settings, CinematicSequenceId sequenceId) noexcept {
    const auto found =
        std::find_if(settings.sequences.begin(), settings.sequences.end(),
                     [sequenceId](const CinematicSequence& sequence) { return sequence.id == sequenceId; });
    return found != settings.sequences.end() ? &*found : nullptr;
}

const CinematicSequence* FindCinematicSequence(const SceneCinematicsSettings& settings,
                                               CinematicSequenceId sequenceId) noexcept {
    const auto found =
        std::find_if(settings.sequences.begin(), settings.sequences.end(),
                     [sequenceId](const CinematicSequence& sequence) { return sequence.id == sequenceId; });
    return found != settings.sequences.end() ? &*found : nullptr;
}

float GetCinematicSequenceContentEnd(const CinematicSequence& sequence) noexcept {
    float effectsEnd = 0.0f;
    for (const auto& effect : sequence.screenEffects)
        effectsEnd = (std::max)(effectsEnd, effect.start + effect.duration);
    return (std::max)({effectsEnd, SEQUENCER::GetCameraCutTrackContentEnd(sequence.cameraCutTrack),
                       SEQUENCER::GetPresentationCueContentEnd(sequence.presentationCueTrack),
                       SEQUENCER::GetSequenceEventContentEnd(sequence.eventTrack)});
}

namespace {
CinematicCameraEvaluation EvaluateCinematicCameraTrackInternal(
    const CinematicSequence& sequence, float timeSeconds,
    const SEQUENCER::SequenceBindingContext* bindingContext) noexcept {
    CinematicCameraEvaluation result{};
    const SEQUENCER::CameraCutTrackEvaluation trackEvaluation =
        SEQUENCER::EvaluateCameraCutTrack(sequence.cameraCutTrack, timeSeconds);
    if (!trackEvaluation.IsValid()) {
        return result;
    }

    SceneObjectId cameraObjectId{};
    const bool resolved =
        bindingContext != nullptr
            ? SEQUENCER::ResolveSceneObjectBinding(sequence.bindings, trackEvaluation.cameraBindingId, *bindingContext,
                                                   cameraObjectId)
            : SEQUENCER::ResolveSceneObjectBinding(sequence.bindings, trackEvaluation.cameraBindingId, cameraObjectId);
    const auto shot = std::find_if(sequence.cameraCutTrack.clips.begin(), sequence.cameraCutTrack.clips.end(),
                                   [&](const auto& clip) { return clip.id == trackEvaluation.clipId; });
    if (!resolved && shot != sequence.cameraCutTrack.clips.end() && shot->camera.useScenePose &&
        shot->camera.keys.empty()) {
        return result;
    }

    result.shotId = trackEvaluation.clipId;
    result.cameraBindingId = trackEvaluation.cameraBindingId;
    result.cameraObjectId = cameraObjectId;
    result.transition = trackEvaluation.transition;
    result.sequenceTimeSeconds = trackEvaluation.sequenceTimeSeconds;
    result.localTimeSeconds = trackEvaluation.localTimeSeconds;
    return result;
}
} // namespace

CinematicCameraEvaluation EvaluateCinematicCameraTrack(const CinematicSequence& sequence, float timeSeconds) noexcept {
    return EvaluateCinematicCameraTrackInternal(sequence, timeSeconds, nullptr);
}

CinematicCameraEvaluation EvaluateCinematicCameraTrack(
    const CinematicSequence& sequence, float timeSeconds,
    const SEQUENCER::SequenceBindingContext& bindingContext) noexcept {
    return EvaluateCinematicCameraTrackInternal(sequence, timeSeconds, &bindingContext);
}

} // namespace HIKARI
