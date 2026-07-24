#pragma once

#include <cstdint>
#include <vector>

#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {

    enum class CameraCutTransitionMode : uint8_t {
        Cut,
        EaseInOut,
    };

    struct CameraCutTransition {
        CameraCutTransitionMode mode = CameraCutTransitionMode::Cut;
        float durationSeconds = 0.0f;
    };

    struct CameraCutClip {
        uint64_t id = 0;
        SequenceBindingId cameraBindingId{};
        float startTimeSeconds = 0.0f;
        float durationSeconds = 2.0f;
        CameraCutTransition transition{};
    };

    struct CameraCutTrack {
        SequenceTrackId id{ 1 };
        bool enabled = true;
        std::vector<CameraCutClip> clips{};
    };

    struct CameraCutTrackEvaluation {
        uint64_t clipId = 0;
        SequenceBindingId cameraBindingId{};
        CameraCutTransition transition{};
        float sequenceTimeSeconds = 0.0f;
        float localTimeSeconds = 0.0f;

        bool IsValid() const noexcept {
            return clipId != 0 && cameraBindingId.IsValid();
        }
    };

    constexpr float kMinCameraCutClipDurationSeconds = 0.1f;
    constexpr float kMaxCameraBlendDurationSeconds = 60.0f;

    void NormalizeCameraCutTrack(CameraCutTrack& track);
    uint64_t AllocateCameraCutClipId(const CameraCutTrack& track);
    float GetCameraCutTrackContentEnd(const CameraCutTrack& track) noexcept;
    CameraCutTrackEvaluation EvaluateCameraCutTrack(
        const CameraCutTrack& track,
        float timeSeconds) noexcept;

} // namespace HIKARI::SEQUENCER
