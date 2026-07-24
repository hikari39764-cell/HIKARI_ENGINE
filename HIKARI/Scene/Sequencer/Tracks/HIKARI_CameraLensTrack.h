#pragma once

#include <cstdint>
#include <vector>

#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {

    struct CameraLensKeyframe {
        uint64_t id = 0;
        float timeSeconds = 0.0f;
        float verticalFovDegrees = 60.0f;
        float nearClip = 0.1f;
        float farClip = 100.0f;
        SequenceInterpolationMode interpolation =
            SequenceInterpolationMode::Smooth;
    };

    struct CameraLensChannel {
        SequenceBindingId cameraBindingId{};
        std::vector<CameraLensKeyframe> keyframes{};
    };

    struct CameraLensTrack {
        SequenceTrackId id{ 3 };
        bool enabled = true;
        std::vector<CameraLensChannel> channels{};
    };

    struct CameraLensTrackEvaluation {
        SequenceBindingId cameraBindingId{};
        float verticalFovDegrees = 60.0f;
        float nearClip = 0.1f;
        float farClip = 100.0f;
        bool valid = false;
    };

    void NormalizeCameraLensTrack(CameraLensTrack& track);
    uint64_t AllocateCameraLensKeyframeId(const CameraLensTrack& track);
    CameraLensChannel* FindCameraLensChannel(
        CameraLensTrack& track,
        SequenceBindingId bindingId) noexcept;
    const CameraLensChannel* FindCameraLensChannel(
        const CameraLensTrack& track,
        SequenceBindingId bindingId) noexcept;
    CameraLensKeyframe* FindCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept;
    bool MoveCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId,
        float timeSeconds);
    uint64_t SetCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds,
        float verticalFovDegrees,
        float nearClip,
        float farClip);
    float GetCameraLensTrackContentEnd(
        const CameraLensTrack& track) noexcept;
    CameraLensTrackEvaluation EvaluateCameraLensTrack(
        const CameraLensTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds) noexcept;

} // namespace HIKARI::SEQUENCER
