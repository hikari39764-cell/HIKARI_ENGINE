#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/Tracks/HIKARI_PresentationCueTrack.h"
#include "Scene/Sequencer/Tracks/HIKARI_CameraCutTrack.h"

namespace HIKARI {

class Camera3D;

struct CinematicSequenceId {
    uint64_t value = 0;

    bool IsValid() const noexcept { return value != 0; }

    bool operator==(const CinematicSequenceId& rhs) const noexcept { return value == rhs.value; }
};

struct CinematicSequence {
    CinematicSequenceId id{1};
    std::string name{"Main Sequence"};
    float durationSeconds = 10.0f;
    SEQUENCER::SequenceBindingTable bindings{};
    SEQUENCER::CameraCutTrack cameraCutTrack{};
    SEQUENCER::PresentationCueTrack presentationCueTrack{};
    std::vector<SEQUENCER::SequenceScreenEffect> screenEffects;
};

struct SceneCinematicsSettings {
    CinematicSequenceId defaultSequenceId{1};
    std::vector<CinematicSequence> sequences{CinematicSequence{}};
};

struct CinematicCameraEvaluation {
    uint64_t shotId = 0;
    SEQUENCER::SequenceBindingId cameraBindingId{};
    SceneObjectId cameraObjectId{};
    SEQUENCER::CameraCutTransition transition{};
    float sequenceTimeSeconds = 0.0f;
    float localTimeSeconds = 0.0f;
    bool IsValid() const noexcept { return shotId != 0; }
};

constexpr float kMinCinematicSequenceDurationSeconds = 1.0f;
constexpr float kMaxCinematicSequenceDurationSeconds = SEQUENCER::kMaxSequenceDurationSeconds;

void NormalizeCinematicSequence(CinematicSequence& sequence);
void NormalizeSceneCinematicsSettings(SceneCinematicsSettings& settings);
CinematicSequenceId AllocateCinematicSequenceId(const SceneCinematicsSettings& settings);
CinematicSequence* FindCinematicSequence(SceneCinematicsSettings& settings, CinematicSequenceId sequenceId) noexcept;
const CinematicSequence* FindCinematicSequence(const SceneCinematicsSettings& settings,
                                               CinematicSequenceId sequenceId) noexcept;
float GetCinematicSequenceContentEnd(const CinematicSequence& sequence) noexcept;
CinematicCameraEvaluation EvaluateCinematicCameraTrack(const CinematicSequence& sequence, float timeSeconds) noexcept;
CinematicCameraEvaluation EvaluateCinematicCameraTrack(
    const CinematicSequence& sequence, float timeSeconds,
    const SEQUENCER::SequenceBindingContext& bindingContext) noexcept;

} // namespace HIKARI
